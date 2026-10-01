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

#include "cuttlefish/host/frontend/webrtc/libcommon/vulkan_video_encoder.h"

#include <iterator>

#include "api/scoped_refptr.h"
#include "api/video/i420_buffer.h"
#include "api/video/video_bitrate_allocation.h"
#include "api/video/video_codec_type.h"
#include "api/video/video_frame.h"
#include "api/video/video_frame_buffer.h"
#include "api/video_codecs/sdp_video_format.h"
#include "api/video_codecs/video_encoder.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "modules/video_coding/include/video_error_codes.h"

#include "cuttlefish/host/frontend/webrtc/libcommon/vulkan_encoder_config.h"

namespace cuttlefish {
namespace {

using ::testing::ElementsAre;

const webrtc::VideoEncoder::ResolutionBitrateLimits kTestBitrateLimits[] = {
    {320 * 240, 80000, 40000, 800000},
    {1280 * 720, 500000, 200000, 4000000},
};

const VulkanEncoderConfig kTestConfig = {
    .webrtc_codec_type = webrtc::kVideoCodecAV1,
    .implementation_name = "VulkanAv1",
    .bitrate_limits = kTestBitrateLimits,
    .bitrate_limits_count = std::size(kTestBitrateLimits),
};

webrtc::SdpVideoFormat Av1Format() {
  return webrtc::SdpVideoFormat("AV1", {{"profile", "0"}});
}

webrtc::VideoFrame SmallI420Frame() {
  const rtc::scoped_refptr<webrtc::VideoFrameBuffer> buffer =
      webrtc::I420Buffer::Create(16, 16);
  return webrtc::VideoFrame::Builder()
      .set_video_frame_buffer(buffer)
      .set_timestamp_rtp(0)
      .build();
}

TEST(VulkanVideoEncoderTest, EncoderInfoComesFromTheConfig) {
  const VulkanVideoEncoder encoder(kTestConfig, Av1Format());

  const webrtc::VideoEncoder::EncoderInfo info = encoder.GetEncoderInfo();
  EXPECT_EQ(info.implementation_name, "VulkanAv1");
  EXPECT_TRUE(info.is_hardware_accelerated);
  EXPECT_TRUE(info.supports_native_handle);
  EXPECT_THAT(info.preferred_pixel_formats,
              ElementsAre(webrtc::VideoFrameBuffer::Type::kNative));
  ASSERT_EQ(info.resolution_bitrate_limits.size(), 2u);
  EXPECT_EQ(info.resolution_bitrate_limits[1].frame_size_pixels, 1280 * 720);
  EXPECT_EQ(info.resolution_bitrate_limits[1].max_bitrate_bps, 4000000);
}

TEST(VulkanVideoEncoderTest, NullCodecSettingsIsAParameterError) {
  VulkanVideoEncoder encoder(kTestConfig, Av1Format());

  const webrtc::VideoEncoder::Capabilities capabilities(false);
  const webrtc::VideoEncoder::Settings settings(capabilities, 1, 1200);
  EXPECT_EQ(encoder.InitEncode(nullptr, settings),
            WEBRTC_VIDEO_CODEC_ERR_PARAMETER);
}

TEST(VulkanVideoEncoderTest, EncodeBeforeInitIsUninitialized) {
  VulkanVideoEncoder encoder(kTestConfig, Av1Format());

  EXPECT_EQ(encoder.Encode(SmallI420Frame(), nullptr),
            WEBRTC_VIDEO_CODEC_UNINITIALIZED);
}

TEST(VulkanVideoEncoderTest, ReleaseWithoutInitSucceeds) {
  VulkanVideoEncoder encoder(kTestConfig, Av1Format());
  EXPECT_EQ(encoder.Release(), WEBRTC_VIDEO_CODEC_OK);
}

TEST(VulkanVideoEncoderTest, SetRatesBeforeInitLeavesTheEncoderUninitialized) {
  VulkanVideoEncoder encoder(kTestConfig, Av1Format());

  webrtc::VideoBitrateAllocation allocation;
  allocation.SetBitrate(0, 0, 2000000);
  encoder.SetRates(
      webrtc::VideoEncoder::RateControlParameters(allocation, 30.0));

  EXPECT_EQ(encoder.Encode(SmallI420Frame(), nullptr),
            WEBRTC_VIDEO_CODEC_UNINITIALIZED);
}

}  // namespace
}  // namespace cuttlefish
