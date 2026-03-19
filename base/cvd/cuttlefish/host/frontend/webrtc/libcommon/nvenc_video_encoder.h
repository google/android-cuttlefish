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

#ifndef CUTTLEFISH_HOST_FRONTEND_WEBRTC_LIBCOMMON_NVENC_VIDEO_ENCODER_H_
#define CUTTLEFISH_HOST_FRONTEND_WEBRTC_LIBCOMMON_NVENC_VIDEO_ENCODER_H_

#include <memory>
#include <vector>

#include "api/video/video_codec_type.h"
#include "api/video/video_frame.h"
#include "api/video_codecs/sdp_video_format.h"
#include "api/video_codecs/video_encoder.h"
#include "modules/video_coding/include/video_codec_interface.h"

#include <cuda.h>
#include <cuda_runtime.h>
#include <nvEncodeAPI.h>

#include "cuttlefish/host/frontend/webrtc/libcommon/cuda_context.h"
#include "cuttlefish/host/frontend/webrtc/libcommon/nvenc_encoder_config.h"

namespace cuttlefish {

// Hardware-accelerated video encoder using NVIDIA NVENC.
//
// This encoder accepts native ARGB/ABGR input buffers from the display
// compositor and uses NVENC's internal RGB-to-YUV conversion for optimal
// performance. No custom CUDA kernels are needed for color space conversion.
//
// Key features:
//   - Native ARGB/ABGR input (no CPU-side color conversion)
//   - CBR rate control optimized for low-latency WebRTC streaming
//   - Ultra-low latency tuning (no B-frames, infinite GOP)
//   - Resolution-based bitrate limits for high-quality streaming
//   - Lazy input resource registration (detects pixel format on first frame)
//
// Thread safety: InitEncode() must be called before Encode(). After
// initialization, Encode() and SetRates() may be called from any thread.
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
                 const std::vector<webrtc::VideoFrameType>* frame_types) override;
  void SetRates(const RateControlParameters& parameters) override;
  EncoderInfo GetEncoderInfo() const override;

 private:
  bool InitCuda();
  bool InitNvenc();
  bool RegisterInputResource(uint32_t pixel_format);
  void DestroyEncoder();
  void Reconfigure(const RateControlParameters& parameters);

  NvencEncoderConfig config_;
  webrtc::SdpVideoFormat format_;
  webrtc::VideoCodec codec_settings_;
  int32_t bitrate_bps_ = 0;
  uint32_t framerate_ = 30;
  bool inited_ = false;

  // CUDA context
  std::shared_ptr<CudaContext> shared_context_;
  CUcontext cuda_context_ = nullptr;
  cudaStream_t cuda_stream_ = nullptr;

  // GPU buffer for RGBA input (NVENC handles RGB->YUV conversion internally)
  void* d_rgba_buffer_ = nullptr;
  NV_ENC_INPUT_PTR nvenc_input_buffer_ = nullptr;
  NV_ENC_OUTPUT_PTR nvenc_output_bitstream_ = nullptr;

  // Input resource registration state (lazy registration on first frame)
  bool input_resource_registered_ = false;
  uint32_t registered_pixel_format_ = 0;
  NV_ENC_BUFFER_FORMAT nvenc_input_format_ = NV_ENC_BUFFER_FORMAT_UNDEFINED;

  // NVENC encoder
  void* encoder_ = nullptr;
  NV_ENCODE_API_FUNCTION_LIST nvenc_funcs_{};

  // Stored initialization params for reconfiguration
  NV_ENC_INITIALIZE_PARAMS stored_init_params_{};
  NV_ENC_CONFIG stored_encode_config_{};

  // Frame dimensions
  int width_ = 0;
  int height_ = 0;
  size_t rgba_pitch_ = 0;

  // Frame counter for periodic logging (per-instance, not static)
  int frame_count_ = 0;

  webrtc::EncodedImageCallback* callback_ = nullptr;
};

}  // namespace cuttlefish

#endif  // CUTTLEFISH_HOST_FRONTEND_WEBRTC_LIBCOMMON_NVENC_VIDEO_ENCODER_H_