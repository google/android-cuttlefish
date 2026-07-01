/*
 * Copyright (C) 2026 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "cuttlefish/host/frontend/webrtc/libcommon/nvenc_video_encoder.h"

#include <algorithm>
#include <vector>

#include <drm/drm_fourcc.h>

#include "absl/log/log.h"
#include "android-base/logging.h"
#include "modules/video_coding/include/video_error_codes.h"

#include "cuttlefish/host/frontend/webrtc/libcommon/abgr_buffer.h"
#include "cuttlefish/host/libs/gpu/cuda_loader.h"
#include "cuttlefish/host/libs/gpu/nvenc_loader.h"
#include "cuttlefish/result/result.h"

namespace cuttlefish {

void NvencEncoderDeleter::operator()(void* encoder) const {
  if (encoder && funcs) {
    funcs->nvEncDestroyEncoder(encoder);
  }
}

namespace {

constexpr int kGpuDeviceId = 0;

std::string GetCudaErrorStr(const CudaFunctions* cuda,
                            CUresult err) {
  const char* str = nullptr;
  cuda->cuGetErrorString(err, &str);
  return str ? str : "unknown";
}

}  // namespace

NvencVideoEncoder::NvencVideoEncoder(const NvencEncoderConfig& config,
                                     const webrtc::SdpVideoFormat& format)
    : config_(config), format_(format) {
  VLOG(1) << "Creating NvencVideoEncoder ("
            << config_.implementation_name
            << ") for format: " << format.ToString();
}

NvencVideoEncoder::~NvencVideoEncoder() {
  DestroyEncoder();
}

int32_t NvencVideoEncoder::Release() {
  DestroyEncoder();
  return WEBRTC_VIDEO_CODEC_OK;
}

void NvencVideoEncoder::DestroyEncoder() {
  if (encoder_) {
    if (nvenc_input_buffer_) {
      nvenc_funcs_->nvEncUnregisterResource(encoder_.get(),
                                           nvenc_input_buffer_);
      nvenc_input_buffer_ = nullptr;
    }
    if (nvenc_output_bitstream_) {
      nvenc_funcs_->nvEncDestroyBitstreamBuffer(encoder_.get(),
                                               nvenc_output_bitstream_);
      nvenc_output_bitstream_ = nullptr;
    }
    encoder_.reset();
  }

  if (cuda_context_ && cuda_) {
    ScopedCudaContext scope(cuda_context_, cuda_);
    if (scope.ok()) {
      if (d_rgba_buffer_) cuda_->cuMemFree(d_rgba_buffer_);
      if (cuda_stream_) cuda_->cuStreamDestroy(cuda_stream_);
    } else {
      LOG(WARNING) << "Failed to push CUDA context during cleanup";
    }
    d_rgba_buffer_ = 0;
    cuda_stream_ = nullptr;
  }

  shared_context_.reset();
  cuda_context_ = nullptr;
  input_resource_registered_ = false;
  registered_pixel_format_ = 0;
  nvenc_input_format_ = NV_ENC_BUFFER_FORMAT_UNDEFINED;

  inited_ = false;
}

int NvencVideoEncoder::InitEncode(
    const webrtc::VideoCodec* codec_settings,
    const webrtc::VideoEncoder::Settings& settings) {
  if (inited_) {
    DestroyEncoder();
  }

  VLOG(1) << "NvencVideoEncoder::InitEncode ("
          << config_.implementation_name << ")";

  if (!codec_settings) {
    LOG(ERROR) << "InitEncode: codec_settings is null";
    return WEBRTC_VIDEO_CODEC_ERR_PARAMETER;
  }

  width_ = codec_settings->width;
  height_ = codec_settings->height;
  bitrate_bps_ = codec_settings->startBitrate * 1000;
  framerate_ = codec_settings->maxFramerate;
  codec_settings_ = *codec_settings;

  VLOG(1) << "InitEncode parameters: "
          << width_ << "x" << height_ << " @" << framerate_
          << "fps, " << bitrate_bps_ << "bps, max="
          << codec_settings->maxBitrate << "kbps";

  if (bitrate_bps_ < config_.min_bitrate_bps) {
    bitrate_bps_ = config_.min_bitrate_bps;
  }

  VLOG(1) << "Initializing CUDA...";
  Result<void> cuda_result = InitCuda();
  if (!cuda_result.ok()) {
    LOG(ERROR) << "InitCuda failed: "
               << cuda_result.error().Message();
    return WEBRTC_VIDEO_CODEC_ERROR;
  }

  VLOG(1) << "Initializing NVENC...";
  Result<void> nvenc_result = InitNvenc();
  if (!nvenc_result.ok()) {
    LOG(ERROR) << "InitNvenc failed: "
               << nvenc_result.error().Message();
    return WEBRTC_VIDEO_CODEC_ERROR;
  }

  inited_ = true;
  return WEBRTC_VIDEO_CODEC_OK;
}

Result<void> NvencVideoEncoder::InitCuda() {
  cuda_ = CF_EXPECT(TryLoadCuda(), "CUDA not available");

  shared_context_ = CudaContext::Get(kGpuDeviceId);
  CF_EXPECT(shared_context_ != nullptr,
            "Failed to get CUDA context for device "
                << kGpuDeviceId);
  cuda_context_ = shared_context_->get();

  ScopedCudaContext scope(cuda_context_, cuda_);
  CF_EXPECT(scope.ok(), "Failed to push CUDA context");

  CUresult err = cuda_->cuStreamCreate(&cuda_stream_, 0);
  CF_EXPECT(err == CUDA_SUCCESS,
            "cuStreamCreate failed: " << GetCudaErrorStr(cuda_, err));

  err = cuda_->cuMemAllocPitch(&d_rgba_buffer_, &rgba_pitch_,
                               width_ * 4, height_, 4);
  CF_EXPECT(err == CUDA_SUCCESS,
            "cuMemAllocPitch failed: " << GetCudaErrorStr(cuda_, err));

  VLOG(1) << "InitCuda: CUDA resources allocated, pitch="
          << rgba_pitch_;
  return {};
}

Result<void> NvencVideoEncoder::InitNvenc() {
  nvenc_funcs_ = CF_EXPECT(TryLoadNvenc(), "NVENC not available");

  NV_ENC_OPEN_ENCODE_SESSION_EX_PARAMS open_params =
      {NV_ENC_OPEN_ENCODE_SESSION_EX_PARAMS_VER};
  open_params.device = cuda_context_;
  open_params.deviceType = NV_ENC_DEVICE_TYPE_CUDA;
  open_params.apiVersion = NVENCAPI_VERSION;

  void* raw_encoder = nullptr;
  NVENCSTATUS status = nvenc_funcs_->nvEncOpenEncodeSessionEx(
      &open_params, &raw_encoder);
  CF_EXPECT(status == NV_ENC_SUCCESS,
            "nvEncOpenEncodeSessionEx failed: " << status);
  encoder_ = NvencEncoderHandle(raw_encoder,
                                NvencEncoderDeleter{nvenc_funcs_});

  stored_encode_config_ = {NV_ENC_CONFIG_VER};

  stored_init_params_ = {NV_ENC_INITIALIZE_PARAMS_VER};
  stored_init_params_.encodeConfig = &stored_encode_config_;
  stored_init_params_.encodeGUID = config_.codec_guid;
  stored_init_params_.presetGUID = NV_ENC_PRESET_P4_GUID;
  stored_init_params_.tuningInfo =
      NV_ENC_TUNING_INFO_ULTRA_LOW_LATENCY;
  stored_init_params_.encodeWidth = width_;
  stored_init_params_.encodeHeight = height_;
  stored_init_params_.darWidth = width_;
  stored_init_params_.darHeight = height_;
  stored_init_params_.frameRateNum = framerate_;
  stored_init_params_.frameRateDen = 1;
  stored_init_params_.enablePTD = 1;
  stored_init_params_.enableEncodeAsync = 0;

  NV_ENC_PRESET_CONFIG preset_config = {NV_ENC_PRESET_CONFIG_VER};
  preset_config.presetCfg = {NV_ENC_CONFIG_VER};

  status = nvenc_funcs_->nvEncGetEncodePresetConfigEx(
      encoder_.get(), stored_init_params_.encodeGUID,
      stored_init_params_.presetGUID,
      stored_init_params_.tuningInfo, &preset_config);
  CF_EXPECT(status == NV_ENC_SUCCESS,
            "nvEncGetEncodePresetConfigEx failed: " << status);

  stored_encode_config_ = preset_config.presetCfg;
  stored_encode_config_.version = NV_ENC_CONFIG_VER;
  stored_encode_config_.profileGUID = config_.profile_guid;

  stored_encode_config_.gopLength = NVENC_INFINITE_GOPLENGTH;
  stored_encode_config_.frameIntervalP = 1;
  stored_encode_config_.rcParams.rateControlMode =
      NV_ENC_PARAMS_RC_CBR;
  stored_encode_config_.rcParams.averageBitRate = bitrate_bps_;
  stored_encode_config_.rcParams.vbvBufferSize = bitrate_bps_;
  stored_encode_config_.rcParams.vbvInitialDelay = bitrate_bps_;
  stored_encode_config_.rcParams.zeroReorderDelay = 1;

  config_.apply_defaults(&stored_encode_config_);

  stored_init_params_.encodeConfig = &stored_encode_config_;

  status = nvenc_funcs_->nvEncInitializeEncoder(
      encoder_.get(), &stored_init_params_);
  CF_EXPECT(status == NV_ENC_SUCCESS,
            "nvEncInitializeEncoder failed: " << status);

  NV_ENC_CREATE_BITSTREAM_BUFFER create_bs =
      {NV_ENC_CREATE_BITSTREAM_BUFFER_VER};
  status = nvenc_funcs_->nvEncCreateBitstreamBuffer(
      encoder_.get(), &create_bs);
  CF_EXPECT(status == NV_ENC_SUCCESS,
            "nvEncCreateBitstreamBuffer failed: " << status);
  nvenc_output_bitstream_ = create_bs.bitstreamBuffer;

  LOG(INFO) << "NVENC initialized: "
            << config_.implementation_name << " "
            << width_ << "x" << height_ << " @"
            << framerate_ << "fps, " << bitrate_bps_
            << "bps";
  return {};
}

Result<void> NvencVideoEncoder::RegisterInputResource(
    uint32_t pixel_format) {
  NV_ENC_BUFFER_FORMAT nvenc_fmt;

  if (pixel_format == DRM_FORMAT_ARGB8888 ||
      pixel_format == DRM_FORMAT_XRGB8888) {
    nvenc_fmt = NV_ENC_BUFFER_FORMAT_ARGB;
  } else if (pixel_format == DRM_FORMAT_ABGR8888 ||
             pixel_format == DRM_FORMAT_XBGR8888) {
    nvenc_fmt = NV_ENC_BUFFER_FORMAT_ABGR;
  } else {
    return CF_ERR("Unsupported pixel format: 0x"
                  << std::hex << pixel_format);
  }

  NV_ENC_REGISTER_RESOURCE reg_res =
      {NV_ENC_REGISTER_RESOURCE_VER};
  reg_res.resourceType = NV_ENC_INPUT_RESOURCE_TYPE_CUDADEVICEPTR;
  reg_res.resourceToRegister =
      reinterpret_cast<void*>(d_rgba_buffer_);
  reg_res.width = width_;
  reg_res.height = height_;
  reg_res.pitch = rgba_pitch_;
  reg_res.bufferFormat = nvenc_fmt;

  NVENCSTATUS status = nvenc_funcs_->nvEncRegisterResource(
      encoder_.get(), &reg_res);
  CF_EXPECT(status == NV_ENC_SUCCESS,
            "nvEncRegisterResource failed: " << status);

  nvenc_input_buffer_ = reg_res.registeredResource;
  nvenc_input_format_ = nvenc_fmt;
  registered_pixel_format_ = pixel_format;
  input_resource_registered_ = true;

  VLOG(1) << "Input resource registered for pixel format 0x"
          << std::hex << pixel_format;
  return {};
}

int32_t NvencVideoEncoder::RegisterEncodeCompleteCallback(
    webrtc::EncodedImageCallback* callback) {
  callback_ = callback;
  return WEBRTC_VIDEO_CODEC_OK;
}

void NvencVideoEncoder::SetRates(
    const RateControlParameters& parameters) {
  if (!inited_) return;

  int32_t new_bitrate = bitrate_bps_;
  if (parameters.bitrate.get_sum_bps() != 0) {
    int32_t requested = static_cast<int32_t>(
        parameters.bitrate.get_sum_bps());
    new_bitrate = std::max(requested, config_.min_bitrate_bps);
    new_bitrate = std::min(new_bitrate, config_.max_bitrate_bps);
  }
  uint32_t new_framerate =
      static_cast<uint32_t>(parameters.framerate_fps);

  if (new_bitrate != bitrate_bps_ || new_framerate != framerate_) {
    bitrate_bps_ = new_bitrate;
    framerate_ = new_framerate;
    Reconfigure(parameters);
  }
}

void NvencVideoEncoder::Reconfigure(
    const RateControlParameters& parameters) {
  if (!encoder_) {
    LOG(WARNING) << "Reconfigure called but encoder not initialized";
    return;
  }

  stored_encode_config_.rcParams.averageBitRate = bitrate_bps_;
  stored_encode_config_.rcParams.vbvBufferSize = bitrate_bps_;
  stored_encode_config_.rcParams.vbvInitialDelay = bitrate_bps_;

  if (parameters.framerate_fps > 0) {
    stored_init_params_.frameRateNum =
        static_cast<uint32_t>(parameters.framerate_fps);
    stored_init_params_.frameRateDen = 1;
  }

  stored_init_params_.encodeConfig = &stored_encode_config_;

  NV_ENC_RECONFIGURE_PARAMS reconfig =
      {NV_ENC_RECONFIGURE_PARAMS_VER};
  reconfig.reInitEncodeParams = stored_init_params_;
  reconfig.resetEncoder = 0;
  reconfig.forceIDR = 0;

  NVENCSTATUS status = nvenc_funcs_->nvEncReconfigureEncoder(
      encoder_.get(), &reconfig);
  if (status != NV_ENC_SUCCESS) {
    LOG(ERROR) << "nvEncReconfigureEncoder failed: "
               << status;
  } else {
    VLOG(1) << "NVENC reconfigured: bitrate="
            << bitrate_bps_ << ", framerate="
            << stored_init_params_.frameRateNum;
  }
}

int32_t NvencVideoEncoder::Encode(
    const webrtc::VideoFrame& frame,
    const std::vector<webrtc::VideoFrameType>* frame_types) {
  if (!inited_ || !callback_) {
    return WEBRTC_VIDEO_CODEC_UNINITIALIZED;
  }

  Result<void> result = EncodeInner(frame, frame_types);
  if (!result.ok()) {
    LOG(ERROR) << "Encode failed: "
               << result.error().Message();
    return WEBRTC_VIDEO_CODEC_ERROR;
  }
  return WEBRTC_VIDEO_CODEC_OK;
}

Result<void> NvencVideoEncoder::EncodeInner(
    const webrtc::VideoFrame& frame,
    const std::vector<webrtc::VideoFrameType>* frame_types) {
  CF_EXPECT(frame.width() == width_ && frame.height() == height_,
            "Resolution changed from " << width_ << "x" << height_
                << " to " << frame.width() << "x"
                << frame.height());

  ScopedCudaContext cuda_scope(cuda_context_, cuda_);
  CF_EXPECT(cuda_scope.ok(), "Failed to push CUDA context");

  rtc::scoped_refptr<webrtc::VideoFrameBuffer> buffer =
      frame.video_frame_buffer();
  CF_EXPECT(buffer->type() == webrtc::VideoFrameBuffer::Type::kNative,
            "Non-native buffer type: "
                << static_cast<int>(buffer->type()));

  AbgrBuffer* rgba_buf = static_cast<AbgrBuffer*>(buffer.get());
  uint32_t pixel_format = rgba_buf->PixelFormat();

  bool is_argb = (pixel_format == DRM_FORMAT_ARGB8888 ||
                  pixel_format == DRM_FORMAT_XRGB8888);
  bool is_abgr = (pixel_format == DRM_FORMAT_ABGR8888 ||
                  pixel_format == DRM_FORMAT_XBGR8888);
  CF_EXPECT(is_argb || is_abgr,
            "Unsupported pixel format: 0x" << std::hex
                << pixel_format);

  if (!input_resource_registered_) {
    CF_EXPECT(RegisterInputResource(pixel_format));
  }

  CF_EXPECT(pixel_format == registered_pixel_format_,
            "Pixel format changed unexpectedly");

  // Upload RGBA data to GPU
  const uint8_t* src_data = rgba_buf->Data();
  int src_stride = rgba_buf->Stride();

  CUDA_MEMCPY2D copy_desc = {};
  copy_desc.srcMemoryType = CU_MEMORYTYPE_HOST;
  copy_desc.srcHost = src_data;
  copy_desc.srcPitch = src_stride;
  copy_desc.dstMemoryType = CU_MEMORYTYPE_DEVICE;
  copy_desc.dstDevice = d_rgba_buffer_;
  copy_desc.dstPitch = rgba_pitch_;
  copy_desc.WidthInBytes = width_ * 4;
  copy_desc.Height = height_;

  CUresult cuda_err =
      cuda_->cuMemcpy2DAsync(&copy_desc, cuda_stream_);
  CF_EXPECT(cuda_err == CUDA_SUCCESS,
            "cuMemcpy2DAsync failed: "
                << GetCudaErrorStr(cuda_, cuda_err));

  cuda_err = cuda_->cuStreamSynchronize(cuda_stream_);
  CF_EXPECT(cuda_err == CUDA_SUCCESS,
            "cuStreamSynchronize failed: "
                << GetCudaErrorStr(cuda_, cuda_err));

  // Map input resource
  NV_ENC_MAP_INPUT_RESOURCE map_input =
      {NV_ENC_MAP_INPUT_RESOURCE_VER};
  map_input.registeredResource = nvenc_input_buffer_;
  NVENCSTATUS status = nvenc_funcs_->nvEncMapInputResource(
      encoder_.get(), &map_input);
  CF_EXPECT(status == NV_ENC_SUCCESS,
            "nvEncMapInputResource failed: " << status);

  // Encode picture
  NV_ENC_PIC_PARAMS pic_params = {NV_ENC_PIC_PARAMS_VER};
  pic_params.pictureStruct = NV_ENC_PIC_STRUCT_FRAME;
  pic_params.inputBuffer = map_input.mappedResource;
  pic_params.bufferFmt = nvenc_input_format_;
  pic_params.inputWidth = width_;
  pic_params.inputHeight = height_;
  pic_params.outputBitstream = nvenc_output_bitstream_;
  pic_params.inputPitch = rgba_pitch_;

  config_.set_pic_params(&pic_params);

  if (frame_types && !frame_types->empty() &&
      (*frame_types)[0] == webrtc::VideoFrameType::kVideoFrameKey) {
    pic_params.encodePicFlags |= NV_ENC_PIC_FLAG_FORCEIDR;
  }

  status = nvenc_funcs_->nvEncEncodePicture(
      encoder_.get(), &pic_params);
  if (status != NV_ENC_SUCCESS) {
    nvenc_funcs_->nvEncUnmapInputResource(
        encoder_.get(), map_input.mappedResource);
    return CF_ERR("nvEncEncodePicture failed: " << status);
  }

  // Unmap input
  nvenc_funcs_->nvEncUnmapInputResource(
      encoder_.get(), map_input.mappedResource);

  // Retrieve bitstream
  NV_ENC_LOCK_BITSTREAM lock_bs = {NV_ENC_LOCK_BITSTREAM_VER};
  lock_bs.outputBitstream = nvenc_output_bitstream_;
  lock_bs.doNotWait = 0;

  status = nvenc_funcs_->nvEncLockBitstream(
      encoder_.get(), &lock_bs);
  CF_EXPECT(status == NV_ENC_SUCCESS,
            "nvEncLockBitstream failed: " << status);

  // Create copies the data, so we can unlock immediately after.
  webrtc::EncodedImage encoded_image;
  encoded_image.SetEncodedData(webrtc::EncodedImageBuffer::Create(
      static_cast<uint8_t*>(lock_bs.bitstreamBufferPtr),
      lock_bs.bitstreamSizeInBytes));
  encoded_image._encodedWidth = width_;
  encoded_image._encodedHeight = height_;
  encoded_image.SetTimestamp(frame.timestamp());
  encoded_image.ntp_time_ms_ = frame.ntp_time_ms();
  encoded_image._frameType =
      (lock_bs.pictureType == NV_ENC_PIC_TYPE_IDR ||
       lock_bs.pictureType == NV_ENC_PIC_TYPE_I)
          ? webrtc::VideoFrameType::kVideoFrameKey
          : webrtc::VideoFrameType::kVideoFrameDelta;

  nvenc_funcs_->nvEncUnlockBitstream(encoder_.get(),
                                    nvenc_output_bitstream_);

  webrtc::CodecSpecificInfo codec_specific = {};
  codec_specific.codecType = config_.webrtc_codec_type;

  callback_->OnEncodedImage(encoded_image, &codec_specific);

  return {};
}

webrtc::VideoEncoder::EncoderInfo NvencVideoEncoder::GetEncoderInfo()
    const {
  EncoderInfo info;
  info.supports_native_handle = true;
  info.implementation_name = config_.implementation_name;
  info.has_trusted_rate_controller = true;
  info.is_hardware_accelerated = true;

  info.preferred_pixel_formats =
      {webrtc::VideoFrameBuffer::Type::kNative};

  info.resolution_bitrate_limits.assign(
      config_.bitrate_limits,
      config_.bitrate_limits + config_.bitrate_limits_count);

  return info;
}

}  // namespace cuttlefish
