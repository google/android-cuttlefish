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

#include <drm/drm_fourcc.h>
#include <math.h>
#include <stddef.h>
#include <stdint.h>

#include <algorithm>
#include <iterator>
#include <memory>
#include <utility>
#include <vector>

#include "api/make_ref_counted.h"
#include "api/scoped_refptr.h"
#include "api/video/encoded_image.h"
#include "api/video/i420_buffer.h"
#include "api/video/video_codec_type.h"
#include "api/video/video_frame.h"
#include "api/video/video_frame_buffer.h"
#include "api/video/video_frame_type.h"
#include "api/video_codecs/sdp_video_format.h"
#include "api/video_codecs/video_codec.h"
#include "api/video_codecs/video_decoder.h"
#include "api/video_codecs/video_encoder.h"
#include "gtest/gtest.h"
#include "modules/video_coding/codecs/av1/dav1d_decoder.h"
#include "modules/video_coding/include/video_codec_interface.h"
#include "modules/video_coding/include/video_error_codes.h"

#include "cuttlefish/host/frontend/webrtc/cvd_abgr_video_frame_buffer.h"
#include "cuttlefish/host/frontend/webrtc/libcommon/abgr_buffer.h"
#include "cuttlefish/host/frontend/webrtc/libcommon/vulkan_encoder_config.h"
#include "cuttlefish/host/frontend/webrtc/libcommon/vulkan_video_encoder.h"
#include "cuttlefish/host/libs/gpu/vulkan_video_context.h"
#include "cuttlefish/host/libs/screen_connector/video_frame_buffer.h"

namespace cuttlefish {
namespace {

constexpr int kWidth = 640;
constexpr int kHeight = 480;
constexpr int kFrameRate = 30;
constexpr unsigned int kBitrateKbps = 2000;
constexpr int kFrameCount = 30;
constexpr int kSecondKeyFrame = 15;
constexpr double kMinLumaPsnrDb = 30.0;

constexpr int kBytesPerPixel = 4;
constexpr int kMaxSample = 255;
constexpr uint32_t kRtpTicksPerFrame = 90000 / kFrameRate;

constexpr int kShiftPerFrame = 7;
constexpr int kCheckerSize = 8;
constexpr int kCheckerAmplitude = 24;

const webrtc::VideoEncoder::ResolutionBitrateLimits kBitrateLimits[] = {
    {kWidth * kHeight, 200000, 80000, 2000000},
};

const VulkanEncoderConfig kConfig = {
    .webrtc_codec_type = webrtc::kVideoCodecAV1,
    .implementation_name = "VulkanAv1",
    .bitrate_limits = kBitrateLimits,
    .bitrate_limits_count = std::size(kBitrateLimits),
    .min_bitrate_bps = 50000,
    .max_bitrate_bps = 12000000,
    .virtual_buffer_size_ms = 200,
    .initial_virtual_buffer_size_ms = 200,
    .quality_level = 3,
};

struct Rgb {
  int red;
  int green;
  int blue;
};

// A moving gradient under a moving checkerboard. The checker edges make a
// shifted or misscaled picture score low.
Rgb SourcePixel(int x, int y, int index) {
  const int shift = index * kShiftPerFrame;
  const bool light =
      ((x + shift) / kCheckerSize + (y + shift) / kCheckerSize) % 2 == 0;
  const int detail = light ? kCheckerAmplitude : -kCheckerAmplitude;
  const int diagonal = kWidth + kHeight;
  return Rgb{
      .red = std::clamp((x + shift) % kWidth * kMaxSample / kWidth + detail, 0,
                        kMaxSample),
      .green = std::clamp((y + shift) % kHeight * kMaxSample / kHeight + detail,
                          0, kMaxSample),
      .blue = std::clamp(
          (x + y + 2 * shift) % diagonal * kMaxSample / diagonal + detail, 0,
          kMaxSample),
  };
}

double Bt709LimitedRangeLuma(const Rgb& rgb) {
  return 16.0 + 219.0 / kMaxSample *
                    (0.2126 * rgb.red + 0.7152 * rgb.green + 0.0722 * rgb.blue);
}

webrtc::VideoFrame SourceFrame(int index) {
  std::vector<uint8_t> pixels(static_cast<size_t>(kWidth) * kHeight *
                              kBytesPerPixel);
  for (int y = 0; y < kHeight; y++) {
    for (int x = 0; x < kWidth; x++) {
      const Rgb rgb = SourcePixel(x, y, index);
      uint8_t* const pixel =
          &pixels[(static_cast<size_t>(y) * kWidth + x) * kBytesPerPixel];
      pixel[0] = static_cast<uint8_t>(rgb.red);
      pixel[1] = static_cast<uint8_t>(rgb.green);
      pixel[2] = static_cast<uint8_t>(rgb.blue);
      pixel[3] = kMaxSample;
    }
  }
  std::shared_ptr<PackedVideoFrameBuffer> packed =
      std::make_shared<CvdAbgrVideoFrameBuffer>(
          kWidth, kHeight, DRM_FORMAT_ABGR8888, kWidth * kBytesPerPixel,
          pixels.data());
  return webrtc::VideoFrame::Builder()
      .set_video_frame_buffer(
          rtc::make_ref_counted<AbgrBuffer>(std::move(packed)))
      .set_timestamp_rtp(index * kRtpTicksPerFrame)
      .build();
}

double LumaPsnr(const webrtc::I420BufferInterface& decoded, int index) {
  double squared_error = 0;
  for (int y = 0; y < kHeight; y++) {
    for (int x = 0; x < kWidth; x++) {
      const double error = decoded.DataY()[y * decoded.StrideY() + x] -
                           Bt709LimitedRangeLuma(SourcePixel(x, y, index));
      squared_error += error * error;
    }
  }
  const double mean_squared_error = squared_error / (kWidth * kHeight);
  return 10.0 * log10(kMaxSample * kMaxSample / mean_squared_error);
}

class EncodedImageCollector : public webrtc::EncodedImageCallback {
 public:
  webrtc::EncodedImageCallback::Result OnEncodedImage(
      const webrtc::EncodedImage& image,
      const webrtc::CodecSpecificInfo* /*codec_specific_info*/) override {
    images_.push_back(image);
    return webrtc::EncodedImageCallback::Result(
        webrtc::EncodedImageCallback::Result::OK);
  }

  const std::vector<webrtc::EncodedImage>& images() const { return images_; }

 private:
  std::vector<webrtc::EncodedImage> images_;
};

class DecodedFrameCollector : public webrtc::DecodedImageCallback {
 public:
  int32_t Decoded(webrtc::VideoFrame& frame) override {
    frames_.push_back(webrtc::I420Buffer::Copy(*frame.video_frame_buffer()));
    return WEBRTC_VIDEO_CODEC_OK;
  }

  const std::vector<rtc::scoped_refptr<webrtc::I420Buffer>>& frames() const {
    return frames_;
  }

 private:
  std::vector<rtc::scoped_refptr<webrtc::I420Buffer>> frames_;
};

webrtc::VideoCodec CodecSettings() {
  webrtc::VideoCodec codec;
  codec.codecType = webrtc::kVideoCodecAV1;
  codec.width = kWidth;
  codec.height = kHeight;
  codec.startBitrate = kBitrateKbps;
  codec.maxBitrate = kBitrateKbps;
  codec.maxFramerate = kFrameRate;
  return codec;
}

std::vector<webrtc::EncodedImage> EncodeSourceFrames() {
  VulkanVideoEncoder encoder(kConfig,
                             webrtc::SdpVideoFormat("AV1", {{"profile", "0"}}));
  EncodedImageCollector collector;
  encoder.RegisterEncodeCompleteCallback(&collector);

  const webrtc::VideoCodec codec = CodecSettings();
  const webrtc::VideoEncoder::Capabilities capabilities(
      /*loss_notification=*/false);
  const webrtc::VideoEncoder::Settings settings(
      capabilities, /*number_of_cores=*/1, /*max_payload_size=*/1200);
  const int init_result = encoder.InitEncode(&codec, settings);
  EXPECT_EQ(init_result, WEBRTC_VIDEO_CODEC_OK);
  if (init_result != WEBRTC_VIDEO_CODEC_OK) {
    return {};
  }

  for (int index = 0; index < kFrameCount; index++) {
    const bool key_frame = index == 0 || index == kSecondKeyFrame;
    const std::vector<webrtc::VideoFrameType> frame_types = {
        key_frame ? webrtc::VideoFrameType::kVideoFrameKey
                  : webrtc::VideoFrameType::kVideoFrameDelta};
    EXPECT_EQ(encoder.Encode(SourceFrame(index), &frame_types),
              WEBRTC_VIDEO_CODEC_OK)
        << "frame " << index;
  }
  return collector.images();
}

std::vector<rtc::scoped_refptr<webrtc::I420Buffer>> DecodeWithDav1d(
    const std::vector<webrtc::EncodedImage>& images) {
  const std::unique_ptr<webrtc::VideoDecoder> decoder =
      webrtc::CreateDav1dDecoder();
  webrtc::VideoDecoder::Settings settings;
  settings.set_codec_type(webrtc::kVideoCodecAV1);
  const bool configured = decoder->Configure(settings);
  EXPECT_TRUE(configured);
  if (!configured) {
    return {};
  }

  DecodedFrameCollector collector;
  decoder->RegisterDecodeCompleteCallback(&collector);
  for (size_t index = 0; index < images.size(); index++) {
    EXPECT_EQ(decoder->Decode(images[index], /*missing_frames=*/false,
                              /*render_time_ms=*/0),
              WEBRTC_VIDEO_CODEC_OK)
        << "frame " << index;
  }
  return collector.frames();
}

TEST(VulkanVideoEncoderRoundTripTest, Dav1dDecodesEveryFrameCloseToTheSource) {
  if (!IsVulkanAv1EncodeSupported()) {
    GTEST_SKIP() << "No Vulkan AV1 encode device";
  }

  const std::vector<webrtc::EncodedImage> encoded = EncodeSourceFrames();
  ASSERT_EQ(encoded.size(), static_cast<size_t>(kFrameCount));
  EXPECT_EQ(encoded[0]._frameType, webrtc::VideoFrameType::kVideoFrameKey);
  EXPECT_EQ(encoded[kSecondKeyFrame]._frameType,
            webrtc::VideoFrameType::kVideoFrameKey);

  const std::vector<rtc::scoped_refptr<webrtc::I420Buffer>> decoded =
      DecodeWithDav1d(encoded);
  ASSERT_EQ(decoded.size(), static_cast<size_t>(kFrameCount));
  for (int index = 0; index < kFrameCount; index++) {
    const webrtc::I420Buffer& frame = *decoded[index];
    ASSERT_EQ(frame.width(), kWidth) << "frame " << index;
    ASSERT_EQ(frame.height(), kHeight) << "frame " << index;
    EXPECT_GT(LumaPsnr(frame, index), kMinLumaPsnrDb) << "frame " << index;
  }
}

}  // namespace
}  // namespace cuttlefish
