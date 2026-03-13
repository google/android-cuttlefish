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
#include <cstdio>
#include <vector>

#include <cuda_runtime.h>
#include <drm/drm_fourcc.h>
#include "cuttlefish/host/frontend/webrtc/libcommon/abgr_buffer.h"

#include "modules/video_coding/include/video_error_codes.h"
#include "rtc_base/logging.h"

namespace cuttlefish {

NvencVideoEncoder::NvencVideoEncoder(const NvencEncoderConfig& config,
                                     const webrtc::SdpVideoFormat& format)
    : config_(config), format_(format) {
  RTC_LOG(LS_INFO) << "Creating NvencVideoEncoder ("
                   << config_.implementation_name
                   << ") for format: " << format.ToString();
}

NvencVideoEncoder::~NvencVideoEncoder() {
  Release();
}

int32_t NvencVideoEncoder::Release() {
  DestroyEncoder();
  return WEBRTC_VIDEO_CODEC_OK;
}

void NvencVideoEncoder::DestroyEncoder() {
  if (encoder_) {
    if (nvenc_input_buffer_) {
      nvenc_funcs_.nvEncUnregisterResource(encoder_, nvenc_input_buffer_);
      nvenc_input_buffer_ = nullptr;
    }
    if (nvenc_output_bitstream_) {
      nvenc_funcs_.nvEncDestroyBitstreamBuffer(encoder_, nvenc_output_bitstream_);
      nvenc_output_bitstream_ = nullptr;
    }
    nvenc_funcs_.nvEncDestroyEncoder(encoder_);
    encoder_ = nullptr;
  }

  if (cuda_context_) {
      ScopedCudaContext scope(cuda_context_);

      if (d_rgba_buffer_) {
        cudaFree(d_rgba_buffer_);
        d_rgba_buffer_ = nullptr;
      }
      if (cuda_stream_) {
        cudaStreamDestroy(cuda_stream_);
        cuda_stream_ = nullptr;
      }
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

  RTC_LOG(LS_INFO) << "=== NvencVideoEncoder::InitEncode ("
                   << config_.implementation_name << ") ===";

  if (!codec_settings) {
    RTC_LOG(LS_ERROR) << "InitEncode: codec_settings is null";
    return WEBRTC_VIDEO_CODEC_ERR_PARAMETER;
  }

  width_ = codec_settings->width;
  height_ = codec_settings->height;
  bitrate_bps_ = codec_settings->startBitrate * 1000;
  framerate_ = codec_settings->maxFramerate;
  codec_settings_ = *codec_settings;

  RTC_LOG(LS_INFO) << "InitEncode parameters:";
  RTC_LOG(LS_INFO) << "  Resolution: " << width_ << "x" << height_;
  RTC_LOG(LS_INFO) << "  Framerate: " << framerate_ << " fps";
  RTC_LOG(LS_INFO) << "  Start Bitrate: " << bitrate_bps_ << " bps";
  RTC_LOG(LS_INFO) << "  Max Bitrate from codec: "
                   << codec_settings->maxBitrate << " kbps";

  // Enforce minimum bitrate
  if (bitrate_bps_ < config_.min_bitrate_bps) {
    bitrate_bps_ = config_.min_bitrate_bps;
  }

  RTC_LOG(LS_INFO) << "Initializing CUDA...";
  if (!InitCuda()) {
      RTC_LOG(LS_ERROR) << "InitCuda() FAILED";
      return WEBRTC_VIDEO_CODEC_ERROR;
  }
  RTC_LOG(LS_INFO) << "InitCuda() succeeded";

  RTC_LOG(LS_INFO) << "Initializing NVENC...";
  if (!InitNvenc()) {
      RTC_LOG(LS_ERROR) << "InitNvenc() FAILED";
      return WEBRTC_VIDEO_CODEC_ERROR;
  }
  RTC_LOG(LS_INFO) << "InitNvenc() succeeded";

  inited_ = true;
  RTC_LOG(LS_INFO) << "=== NvencVideoEncoder initialized successfully ===";
  RTC_LOG(LS_INFO) << "Note: Input resource will be registered on first "
                   << "frame (lazy init)";
  return WEBRTC_VIDEO_CODEC_OK;
}

bool NvencVideoEncoder::InitCuda() {
    RTC_LOG(LS_INFO) << "InitCuda: Getting shared CUDA context for "
                     << "device 0...";
    shared_context_ = CudaContext::Get(0);
    if (!shared_context_) {
        RTC_LOG(LS_ERROR) << "InitCuda: Failed to get CUDA context";
        return false;
    }
    cuda_context_ = shared_context_->get();
    RTC_LOG(LS_INFO) << "InitCuda: Got CUDA context: " << cuda_context_;

    ScopedCudaContext scope(cuda_context_);
    if (!scope.ok()) {
        RTC_LOG(LS_ERROR) << "InitCuda: Failed to push CUDA context";
        return false;
    }

    RTC_LOG(LS_INFO) << "InitCuda: Creating CUDA stream...";
    cudaError_t err = cudaStreamCreate(&cuda_stream_);
    if (err != cudaSuccess) {
        RTC_LOG(LS_ERROR) << "cudaStreamCreate failed: "
                          << cudaGetErrorString(err);
        return false;
    }
    RTC_LOG(LS_INFO) << "InitCuda: CUDA stream created";

    // Allocate RGBA buffer (4 bytes per pixel)
    // NVENC will handle RGB->YUV conversion internally
    RTC_LOG(LS_INFO) << "InitCuda: Allocating RGBA GPU buffer ("
                     << width_ << "x" << height_
                     << " @ 4 bytes/pixel)...";
    err = cudaMallocPitch(&d_rgba_buffer_, &rgba_pitch_,
                          width_ * 4, height_);
    if (err != cudaSuccess) {
        RTC_LOG(LS_ERROR) << "cudaMallocPitch (RGBA) failed: "
                          << cudaGetErrorString(err);
        return false;
    }
    RTC_LOG(LS_INFO) << "InitCuda: RGBA buffer allocated, pitch="
                     << rgba_pitch_;

    RTC_LOG(LS_INFO) << "InitCuda: CUDA resources allocated successfully";
    return true;
}

bool NvencVideoEncoder::InitNvenc() {
    RTC_LOG(LS_INFO) << "InitNvenc: Loading NVENC API (version "
                     << NVENCAPI_MAJOR_VERSION << "."
                     << NVENCAPI_MINOR_VERSION << ")...";

    // Load NVENC API
    nvenc_funcs_ = {NV_ENCODE_API_FUNCTION_LIST_VER};
    NVENCSTATUS status = NvEncodeAPICreateInstance(&nvenc_funcs_);
    if (status != NV_ENC_SUCCESS) {
        RTC_LOG(LS_ERROR) << "NvEncodeAPICreateInstance failed: "
                          << status;
        RTC_LOG(LS_ERROR) << "Check that libnvidia-encode.so is "
                          << "installed and matches driver version";
        return false;
    }
    RTC_LOG(LS_INFO) << "InitNvenc: NVENC API loaded successfully";

    // Open Encode Session
    RTC_LOG(LS_INFO) << "InitNvenc: Opening encode session with "
                     << "CUDA context " << cuda_context_;
    NV_ENC_OPEN_ENCODE_SESSION_EX_PARAMS open_params =
        {NV_ENC_OPEN_ENCODE_SESSION_EX_PARAMS_VER};
    open_params.device = cuda_context_;
    open_params.deviceType = NV_ENC_DEVICE_TYPE_CUDA;
    open_params.apiVersion = NVENCAPI_VERSION;

    status = nvenc_funcs_.nvEncOpenEncodeSessionEx(
        &open_params, &encoder_);
    if (status != NV_ENC_SUCCESS) {
        RTC_LOG(LS_ERROR) << "nvEncOpenEncodeSessionEx failed: "
                          << status;
        RTC_LOG(LS_ERROR) << "Possible causes: GPU doesn't support "
                          << "NVENC, or encoder sessions exhausted";
        return false;
    }
    RTC_LOG(LS_INFO) << "InitNvenc: Encode session opened, encoder="
                     << encoder_;

    // Initialize stored config struct
    stored_encode_config_ = {NV_ENC_CONFIG_VER};

    // Configure Encoder
    RTC_LOG(LS_INFO) << "InitNvenc: Configuring "
                     << config_.implementation_name << " encoder...";
    RTC_LOG(LS_INFO) << "InitNvenc:   Preset: P4 (Balanced)";
    RTC_LOG(LS_INFO) << "InitNvenc:   Tuning: Ultra Low Latency";
    RTC_LOG(LS_INFO) << "InitNvenc:   Resolution: " << width_ << "x"
                     << height_;
    RTC_LOG(LS_INFO) << "InitNvenc:   Framerate: " << framerate_
                     << " fps";

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

    // Load preset defaults
    RTC_LOG(LS_INFO) << "InitNvenc: Loading preset configuration...";
    NV_ENC_PRESET_CONFIG preset_config = {NV_ENC_PRESET_CONFIG_VER};
    preset_config.presetCfg = {NV_ENC_CONFIG_VER};

    status = nvenc_funcs_.nvEncGetEncodePresetConfigEx(
        encoder_, stored_init_params_.encodeGUID,
        stored_init_params_.presetGUID,
        stored_init_params_.tuningInfo, &preset_config);

    if (status != NV_ENC_SUCCESS) {
        RTC_LOG(LS_ERROR) << "nvEncGetEncodePresetConfigEx failed: "
                          << status;
        return false;
    }
    RTC_LOG(LS_INFO) << "InitNvenc: Preset configuration loaded";

    // Copy preset config to stored config
    stored_encode_config_ = preset_config.presetCfg;
    stored_encode_config_.version = NV_ENC_CONFIG_VER;

    // Custom overrides for ultra-low latency WebRTC streaming
    stored_encode_config_.gopLength = NVENC_INFINITE_GOPLENGTH;
    stored_encode_config_.frameIntervalP = 1;  // No B-frames
    stored_encode_config_.rcParams.rateControlMode =
        NV_ENC_PARAMS_RC_CBR;
    stored_encode_config_.rcParams.averageBitRate = bitrate_bps_;
    stored_encode_config_.rcParams.vbvBufferSize = bitrate_bps_;
    stored_encode_config_.rcParams.vbvInitialDelay = bitrate_bps_;
    stored_encode_config_.rcParams.zeroReorderDelay = 1;

    // Apply codec-specific configuration
    config_.apply_defaults(&stored_encode_config_);

    // Ensure encodeConfig points to stored config
    stored_init_params_.encodeConfig = &stored_encode_config_;

    // Initialize encoder
    RTC_LOG(LS_INFO) << "InitNvenc: Initializing encoder...";
    status = nvenc_funcs_.nvEncInitializeEncoder(
        encoder_, &stored_init_params_);
    if (status != NV_ENC_SUCCESS) {
        RTC_LOG(LS_ERROR) << "nvEncInitializeEncoder failed: "
                          << status;
        RTC_LOG(LS_ERROR) << "Check that resolution/framerate are "
                          << "supported by the GPU";
        return false;
    }

    RTC_LOG(LS_INFO) << "InitNvenc: Encoder initialized successfully";
    RTC_LOG(LS_INFO) << "InitNvenc:   " << width_ << "x" << height_
                     << " @" << framerate_ << "fps, "
                     << bitrate_bps_ << "bps";
    RTC_LOG(LS_INFO) << "InitNvenc:   Rate control: CBR, "
                     << "GOP: infinite, B-frames: 0";

    // NOTE: Input resource registration is deferred to first Encode()
    // call to detect the actual pixel format (ARGB vs ABGR)

    // Create Output Bitstream Buffer
    RTC_LOG(LS_INFO) << "InitNvenc: Creating output bitstream "
                     << "buffer...";
    NV_ENC_CREATE_BITSTREAM_BUFFER create_bs =
        {NV_ENC_CREATE_BITSTREAM_BUFFER_VER};
    status = nvenc_funcs_.nvEncCreateBitstreamBuffer(
        encoder_, &create_bs);
    if (status != NV_ENC_SUCCESS) {
        RTC_LOG(LS_ERROR) << "nvEncCreateBitstreamBuffer failed: "
                          << status;
        return false;
    }
    nvenc_output_bitstream_ = create_bs.bitstreamBuffer;
    RTC_LOG(LS_INFO) << "InitNvenc: Output bitstream buffer created: "
                     << nvenc_output_bitstream_;

    RTC_LOG(LS_INFO) << "=== NVENC initialization complete "
                     << "(input resource pending) ===";
    return true;
}

bool NvencVideoEncoder::RegisterInputResource(uint32_t pixel_format) {
    // Map DRM format to NVENC buffer format
    // DRM_FORMAT_ARGB8888: Memory [B][G][R][A] = NV_ENC_BUFFER_FORMAT_ARGB
    // DRM_FORMAT_ABGR8888: Memory [R][G][B][A] = NV_ENC_BUFFER_FORMAT_ABGR
    NV_ENC_BUFFER_FORMAT nvenc_fmt;
    const char* fmt_name;

    if (pixel_format == DRM_FORMAT_ARGB8888 ||
        pixel_format == DRM_FORMAT_XRGB8888) {
        nvenc_fmt = NV_ENC_BUFFER_FORMAT_ARGB;
        fmt_name = "ARGB";
    } else if (pixel_format == DRM_FORMAT_ABGR8888 ||
               pixel_format == DRM_FORMAT_XBGR8888) {
        nvenc_fmt = NV_ENC_BUFFER_FORMAT_ABGR;
        fmt_name = "ABGR";
    } else {
        char hex_buf[16];
        snprintf(hex_buf, sizeof(hex_buf), "0x%08X", pixel_format);
        RTC_LOG(LS_ERROR) << "RegisterInputResource: Unsupported "
                          << "pixel format: " << hex_buf;
        return false;
    }

    char hex_buf[16];
    snprintf(hex_buf, sizeof(hex_buf), "0x%08X", pixel_format);
    RTC_LOG(LS_INFO) << "RegisterInputResource: Detected format "
                     << fmt_name << " (DRM " << hex_buf << ")";
    RTC_LOG(LS_INFO) << "RegisterInputResource: Using NVENC native "
                     << fmt_name
                     << " input (GPU handles RGB->YUV conversion)";

    // Register RGBA GPU buffer with NVENC
    RTC_LOG(LS_INFO) << "RegisterInputResource: Registering RGBA "
                     << "GPU buffer...";
    RTC_LOG(LS_INFO) << "RegisterInputResource:   Buffer: "
                     << d_rgba_buffer_ << ", pitch=" << rgba_pitch_;

    NV_ENC_REGISTER_RESOURCE reg_res =
        {NV_ENC_REGISTER_RESOURCE_VER};
    reg_res.resourceType = NV_ENC_INPUT_RESOURCE_TYPE_CUDADEVICEPTR;
    reg_res.resourceToRegister = d_rgba_buffer_;
    reg_res.width = width_;
    reg_res.height = height_;
    reg_res.pitch = rgba_pitch_;
    reg_res.bufferFormat = nvenc_fmt;

    NVENCSTATUS status = nvenc_funcs_.nvEncRegisterResource(
        encoder_, &reg_res);
    if (status != NV_ENC_SUCCESS) {
        RTC_LOG(LS_ERROR) << "nvEncRegisterResource failed: "
                          << status;
        return false;
    }

    nvenc_input_buffer_ = reg_res.registeredResource;
    nvenc_input_format_ = nvenc_fmt;
    registered_pixel_format_ = pixel_format;
    input_resource_registered_ = true;

    RTC_LOG(LS_INFO) << "RegisterInputResource: Input resource "
                     << "registered successfully";
    RTC_LOG(LS_INFO) << "RegisterInputResource:   Handle: "
                     << nvenc_input_buffer_;
    RTC_LOG(LS_INFO) << "RegisterInputResource:   NVENC will use "
                     << "CUDA internally for RGB->YUV conversion";

    return true;
}

int32_t NvencVideoEncoder::RegisterEncodeCompleteCallback(
    webrtc::EncodedImageCallback* callback) {
  callback_ = callback;
  return WEBRTC_VIDEO_CODEC_OK;
}

void NvencVideoEncoder::SetRates(
    const RateControlParameters& parameters) {
  if (!inited_) return;

  if (parameters.bitrate.get_sum_bps() != 0) {
      int32_t requested_bitrate = parameters.bitrate.get_sum_bps();

      // Clamp bitrate to configured min/max range
      bitrate_bps_ = std::max(requested_bitrate,
                              config_.min_bitrate_bps);
      bitrate_bps_ = std::min(bitrate_bps_,
                              config_.max_bitrate_bps);

      Reconfigure(parameters);
  }
  framerate_ = parameters.framerate_fps;
}

void NvencVideoEncoder::Reconfigure(
    const RateControlParameters& parameters) {
    if (!encoder_) {
        RTC_LOG(LS_WARNING) << "Reconfigure called but encoder not "
                            << "initialized";
        return;
    }

    // Update stored config with new bitrate
    stored_encode_config_.rcParams.averageBitRate = bitrate_bps_;
    stored_encode_config_.rcParams.vbvBufferSize = bitrate_bps_;
    stored_encode_config_.rcParams.vbvInitialDelay = bitrate_bps_;

    // Update framerate if changed
    if (parameters.framerate_fps > 0) {
        stored_init_params_.frameRateNum =
            static_cast<uint32_t>(parameters.framerate_fps);
        stored_init_params_.frameRateDen = 1;
    }

    // Ensure encodeConfig points to our stored config
    stored_init_params_.encodeConfig = &stored_encode_config_;

    // Build reconfigure params using stored init params
    NV_ENC_RECONFIGURE_PARAMS reconfig =
        {NV_ENC_RECONFIGURE_PARAMS_VER};
    reconfig.reInitEncodeParams = stored_init_params_;
    reconfig.resetEncoder = 0;
    reconfig.forceIDR = 0;

    NVENCSTATUS status = nvenc_funcs_.nvEncReconfigureEncoder(
        encoder_, &reconfig);
    if (status != NV_ENC_SUCCESS) {
        RTC_LOG(LS_ERROR) << "nvEncReconfigureEncoder failed: "
                          << status;
    } else {
        RTC_LOG(LS_INFO) << "NVENC reconfigured: bitrate="
                         << bitrate_bps_ << ", framerate="
                         << stored_init_params_.frameRateNum;
    }
}

int32_t NvencVideoEncoder::Encode(
    const webrtc::VideoFrame& frame,
    const std::vector<webrtc::VideoFrameType>* frame_types) {

  if (++frame_count_ % 300 == 1) {
    RTC_LOG(LS_INFO) << "NVENC Encode() called, frame #"
                     << frame_count_;
  }

  if (!inited_ || !callback_) {
    return WEBRTC_VIDEO_CODEC_UNINITIALIZED;
  }

  // Detect dynamic resolution changes. WebRTC's VideoStreamAdapter
  // can downscale frames without calling InitEncode() again.
  if (frame.width() != width_ || frame.height() != height_) {
    RTC_LOG(LS_WARNING) << "Resolution changed from "
                        << width_ << "x" << height_ << " to "
                        << frame.width() << "x" << frame.height();
    return WEBRTC_VIDEO_CODEC_ERROR;
  }

  // Push CUDA context for this thread
  ScopedCudaContext cuda_scope(cuda_context_);
  if (!cuda_scope.ok()) {
    RTC_LOG(LS_ERROR) << "Failed to push CUDA context in Encode()";
    return WEBRTC_VIDEO_CODEC_ERROR;
  }

  rtc::scoped_refptr<webrtc::VideoFrameBuffer> buffer =
      frame.video_frame_buffer();
  cudaError_t cuda_err;

  // Check for RGBA input (native buffer from gfxstream/Wayland)
  if (buffer->type() != webrtc::VideoFrameBuffer::Type::kNative) {
    static int non_native_count = 0;
    if (++non_native_count <= 5) {
      RTC_LOG(LS_ERROR) << "NVENC: Non-native buffer type: "
                        << static_cast<int>(buffer->type())
                        << " (count=" << non_native_count << ")";
    }
    return WEBRTC_VIDEO_CODEC_ERROR;
  }

  AbgrBuffer* rgba_buf = static_cast<AbgrBuffer*>(buffer.get());
  uint32_t pixel_format = rgba_buf->PixelFormat();

  // Validate pixel format
  bool is_argb = (pixel_format == DRM_FORMAT_ARGB8888 ||
                  pixel_format == DRM_FORMAT_XRGB8888);
  bool is_abgr = (pixel_format == DRM_FORMAT_ABGR8888 ||
                  pixel_format == DRM_FORMAT_XBGR8888);

  if (!is_argb && !is_abgr) {
    char hex_buf[16];
    snprintf(hex_buf, sizeof(hex_buf), "0x%08X", pixel_format);
    RTC_LOG(LS_ERROR) << "Unsupported pixel format: " << hex_buf;
    return WEBRTC_VIDEO_CODEC_ERROR;
  }

  // Lazy registration of input resource on first frame
  if (!input_resource_registered_) {
    if (!RegisterInputResource(pixel_format)) {
      RTC_LOG(LS_ERROR) << "Failed to register input resource";
      return WEBRTC_VIDEO_CODEC_ERROR;
    }
  }

  // Verify format hasn't changed (shouldn't happen in normal
  // operation)
  if (pixel_format != registered_pixel_format_) {
    char old_fmt[16], new_fmt[16];
    snprintf(old_fmt, sizeof(old_fmt), "0x%08X",
             registered_pixel_format_);
    snprintf(new_fmt, sizeof(new_fmt), "0x%08X", pixel_format);
    RTC_LOG(LS_WARNING) << "Pixel format changed from " << old_fmt
                        << " to " << new_fmt
                        << " - this is unexpected";
    return WEBRTC_VIDEO_CODEC_ERROR;
  }

  // Upload RGBA data directly to GPU
  const uint8_t* src_data = rgba_buf->Data();
  int src_stride = rgba_buf->Stride();

  cuda_err = cudaMemcpy2DAsync(d_rgba_buffer_, rgba_pitch_,
                               src_data, src_stride,
                               width_ * 4, height_,
                               cudaMemcpyHostToDevice, cuda_stream_);
  if (cuda_err != cudaSuccess) {
    RTC_LOG(LS_ERROR) << "cudaMemcpy2DAsync (RGBA) failed: "
                      << cudaGetErrorString(cuda_err);
    return WEBRTC_VIDEO_CODEC_ERROR;
  }

  // Synchronize before encoding
  cuda_err = cudaStreamSynchronize(cuda_stream_);
  if (cuda_err != cudaSuccess) {
    RTC_LOG(LS_ERROR) << "cudaStreamSynchronize failed: "
                      << cudaGetErrorString(cuda_err);
    return WEBRTC_VIDEO_CODEC_ERROR;
  }

  // Map Input Resource
  NV_ENC_MAP_INPUT_RESOURCE map_input =
      {NV_ENC_MAP_INPUT_RESOURCE_VER};
  map_input.registeredResource = nvenc_input_buffer_;
  NVENCSTATUS status = nvenc_funcs_.nvEncMapInputResource(
      encoder_, &map_input);
  if (status != NV_ENC_SUCCESS) {
      RTC_LOG(LS_ERROR) << "nvEncMapInputResource failed: " << status;
      return WEBRTC_VIDEO_CODEC_ERROR;
  }

  // Encode Picture - NVENC handles RGB->YUV conversion internally
  NV_ENC_PIC_PARAMS pic_params = {NV_ENC_PIC_PARAMS_VER};
  pic_params.pictureStruct = NV_ENC_PIC_STRUCT_FRAME;
  pic_params.inputBuffer = map_input.mappedResource;
  pic_params.bufferFmt = nvenc_input_format_;  // ARGB or ABGR
  pic_params.inputWidth = width_;
  pic_params.inputHeight = height_;
  pic_params.outputBitstream = nvenc_output_bitstream_;
  pic_params.inputPitch = rgba_pitch_;

  // Set per-frame codec params FIRST.
  config_.set_pic_params(&pic_params);

  // THEN check for keyframe requests. Use |= to preserve flags.
  if (frame_types && !frame_types->empty() &&
      (*frame_types)[0] == webrtc::VideoFrameType::kVideoFrameKey) {
    pic_params.encodePicFlags |= NV_ENC_PIC_FLAG_FORCEIDR;
  }

  status = nvenc_funcs_.nvEncEncodePicture(encoder_, &pic_params);
  if (status != NV_ENC_SUCCESS) {
      RTC_LOG(LS_ERROR) << "nvEncEncodePicture failed: " << status;
      nvenc_funcs_.nvEncUnmapInputResource(
          encoder_, map_input.mappedResource);
      return WEBRTC_VIDEO_CODEC_ERROR;
  }

  // Unmap Input
  nvenc_funcs_.nvEncUnmapInputResource(
      encoder_, map_input.mappedResource);

  // Retrieve Bitstream
  NV_ENC_LOCK_BITSTREAM lock_bs = {NV_ENC_LOCK_BITSTREAM_VER};
  lock_bs.outputBitstream = nvenc_output_bitstream_;
  lock_bs.doNotWait = 0;

  status = nvenc_funcs_.nvEncLockBitstream(encoder_, &lock_bs);
  if (status != NV_ENC_SUCCESS) {
      RTC_LOG(LS_ERROR) << "nvEncLockBitstream failed: " << status;
      return WEBRTC_VIDEO_CODEC_ERROR;
  }

  // Send to WebRTC
  webrtc::EncodedImage encoded_image;
  encoded_image.SetEncodedData(webrtc::EncodedImageBuffer::Create(
      static_cast<uint8_t*>(lock_bs.bitstreamBufferPtr),
      lock_bs.bitstreamSizeInBytes));
  encoded_image._encodedWidth = width_;
  encoded_image._encodedHeight = height_;
  encoded_image.SetTimestamp(frame.timestamp());
  encoded_image.ntp_time_ms_ = frame.ntp_time_ms();
  // H.264 signals keyframes as IDR; AV1 uses I-frames (no IDR concept).
  // Check both so this works for any codec.
  encoded_image._frameType =
      (lock_bs.pictureType == NV_ENC_PIC_TYPE_IDR ||
       lock_bs.pictureType == NV_ENC_PIC_TYPE_I)
          ? webrtc::VideoFrameType::kVideoFrameKey
          : webrtc::VideoFrameType::kVideoFrameDelta;

  webrtc::CodecSpecificInfo codec_specific = {};
  codec_specific.codecType = config_.webrtc_codec_type;

  callback_->OnEncodedImage(encoded_image, &codec_specific);

  nvenc_funcs_.nvEncUnlockBitstream(encoder_, nvenc_output_bitstream_);

  return WEBRTC_VIDEO_CODEC_OK;
}

webrtc::VideoEncoder::EncoderInfo NvencVideoEncoder::GetEncoderInfo()
    const {
  EncoderInfo info;
  info.supports_native_handle = true;
  info.implementation_name = config_.implementation_name;
  info.has_trusted_rate_controller = true;
  info.is_hardware_accelerated = true;

  // CRITICAL: Tell WebRTC we prefer native RGBA buffers, NOT I420.
  // Without this, WebRTC defaults to kI420 and does expensive
  // CPU-based RGBA->I420 conversion before calling Encode(), wasting
  // CPU cycles. NVENC handles ARGB/ABGR natively and does RGB->YUV
  // conversion on GPU.
  info.preferred_pixel_formats =
      {webrtc::VideoFrameBuffer::Type::kNative};

  // Set resolution-based bitrate limits from config.
  info.resolution_bitrate_limits.assign(
      config_.bitrate_limits,
      config_.bitrate_limits + config_.bitrate_limits_count);

  return info;
}

}  // namespace cuttlefish
