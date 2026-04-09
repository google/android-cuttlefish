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

#pragma once

#include <memory>
#include <vector>

#include <cuda.h>
#include <nvEncodeAPI.h>

#include "api/video/video_codec_type.h"
#include "api/video/video_frame.h"
#include "api/video_codecs/sdp_video_format.h"
#include "api/video_codecs/video_encoder.h"
#include "modules/video_coding/include/video_codec_interface.h"

#include "cuttlefish/host/frontend/webrtc/libcommon/nvenc_encoder_config.h"
#include "cuttlefish/host/libs/gpu/cuda_context.h"
#include "cuttlefish/result/result.h"

namespace cuttlefish {

struct CudaFunctions;

struct NvencEncoderDeleter {
  const NV_ENCODE_API_FUNCTION_LIST* funcs = nullptr;
  void operator()(void* encoder) const;
};

using NvencEncoderHandle =
    std::unique_ptr<void, NvencEncoderDeleter>;

// Hardware-accelerated video encoder using NVIDIA NVENC.
//
// Accepts native ARGB/ABGR buffers and uses NVENC's internal RGB-to-YUV
// conversion. CUDA and NVENC libraries are loaded at runtime via dlopen,
// so this links without any CUDA shared libraries present.
class NvencVideoEncoder : public webrtc::VideoEncoder {
 public:
  NvencVideoEncoder(const NvencEncoderConfig& config,
                    const webrtc::SdpVideoFormat& format);
  ~NvencVideoEncoder() override;

  int InitEncode(const webrtc::VideoCodec* codec_settings,
                 const webrtc::VideoEncoder::Settings& settings) override;
  int32_t RegisterEncodeCompleteCallback(
      webrtc::EncodedImageCallback* callback) override;
  int32_t Release() override;
  int32_t Encode(const webrtc::VideoFrame& frame,
                 const std::vector<webrtc::VideoFrameType>* frame_types)
      override;
  void SetRates(const RateControlParameters& parameters) override;
  EncoderInfo GetEncoderInfo() const override;

 private:
  Result<void> InitCuda();
  Result<void> InitNvenc();
  Result<void> RegisterInputResource(uint32_t pixel_format);
  Result<void> EncodeInner(
      const webrtc::VideoFrame& frame,
      const std::vector<webrtc::VideoFrameType>* frame_types);
  void DestroyEncoder();
  void Reconfigure(const RateControlParameters& parameters);

  NvencEncoderConfig config_;
  webrtc::SdpVideoFormat format_;
  webrtc::VideoCodec codec_settings_;
  int32_t bitrate_bps_ = 0;
  uint32_t framerate_ = 30;
  bool inited_ = false;

  // Dynamically loaded CUDA functions
  const CudaFunctions* cuda_ = nullptr;

  // CUDA context
  std::shared_ptr<CudaContext> shared_context_;
  CUcontext cuda_context_ = nullptr;
  CUstream cuda_stream_ = nullptr;

  // GPU buffer for RGBA input
  CUdeviceptr d_rgba_buffer_ = 0;
  NV_ENC_INPUT_PTR nvenc_input_buffer_ = nullptr;
  NV_ENC_OUTPUT_PTR nvenc_output_bitstream_ = nullptr;

  // Input resource registration (lazy, on first frame)
  bool input_resource_registered_ = false;
  uint32_t registered_pixel_format_ = 0;
  NV_ENC_BUFFER_FORMAT nvenc_input_format_ =
      NV_ENC_BUFFER_FORMAT_UNDEFINED;

  // NVENC encoder session
  NvencEncoderHandle encoder_;
  const NV_ENCODE_API_FUNCTION_LIST* nvenc_funcs_ = nullptr;

  // Stored initialization params for reconfiguration
  NV_ENC_INITIALIZE_PARAMS stored_init_params_{};
  NV_ENC_CONFIG stored_encode_config_{};

  // Frame dimensions
  int width_ = 0;
  int height_ = 0;
  size_t rgba_pitch_ = 0;

  webrtc::EncodedImageCallback* callback_ = nullptr;
};

}  // namespace cuttlefish
