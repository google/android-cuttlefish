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

#include <stdint.h>

#include <memory>
#include <vector>

#include "api/video/video_frame.h"
#include "api/video/video_frame_type.h"
#include "api/video_codecs/sdp_video_format.h"
#include "api/video_codecs/video_codec.h"
#include "api/video_codecs/video_encoder.h"

#include "cuttlefish/host/frontend/webrtc/libcommon/vulkan_encoder_config.h"
#include "cuttlefish/host/libs/gpu/vulkan_av1_encode_session.h"
#include "cuttlefish/result/result.h"

namespace cuttlefish {

// Hardware AV1 encoder on top of VulkanAv1EncodeSession. Takes native
// ABGR/ARGB buffers. Used from one thread, WebRTC's encoder queue.
class VulkanVideoEncoder : public webrtc::VideoEncoder {
 public:
  VulkanVideoEncoder(const VulkanEncoderConfig& config,
                     const webrtc::SdpVideoFormat& format);
  ~VulkanVideoEncoder() override;

  int InitEncode(const webrtc::VideoCodec* codec_settings,
                 const webrtc::VideoEncoder::Settings& settings) override;
  int32_t RegisterEncodeCompleteCallback(
      webrtc::EncodedImageCallback* callback) override;
  int32_t Release() override;
  int32_t Encode(
      const webrtc::VideoFrame& frame,
      const std::vector<webrtc::VideoFrameType>* frame_types) override;
  void SetRates(const RateControlParameters& parameters) override;
  EncoderInfo GetEncoderInfo() const override;

 private:
  Result<void> InitEncodeInner();
  Result<void> EncodeInner(const webrtc::VideoFrame& frame);

  VulkanEncoderConfig config_;
  uint32_t width_ = 0;
  uint32_t height_ = 0;
  webrtc::EncodedImageCallback* callback_ = nullptr;
  std::unique_ptr<VulkanAv1EncodeSession> session_;
};

}  // namespace cuttlefish
