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

#include <drm/drm_fourcc.h>
#include <stddef.h>
#include <stdint.h>

#include <memory>
#include <vector>

#include "absl/log/log.h"
#include "api/scoped_refptr.h"
#include "api/video/encoded_image.h"
#include "api/video/video_frame.h"
#include "api/video/video_frame_buffer.h"
#include "api/video/video_frame_type.h"
#include "api/video_codecs/sdp_video_format.h"
#include "api/video_codecs/video_codec.h"
#include "api/video_codecs/video_encoder.h"
#include "modules/video_coding/include/video_codec_interface.h"
#include "modules/video_coding/include/video_error_codes.h"
#include "vulkan/vulkan_core.h"

#include "cuttlefish/host/frontend/webrtc/libcommon/abgr_buffer.h"
#include "cuttlefish/host/frontend/webrtc/libcommon/vulkan_encoder_config.h"
#include "cuttlefish/host/libs/gpu/rgba_to_nv12.h"
#include "cuttlefish/host/libs/gpu/vulkan_av1_encode_session.h"
#include "cuttlefish/host/libs/gpu/vulkan_av1_encode_settings.h"
#include "cuttlefish/result/result.h"

namespace cuttlefish {
namespace {

struct FrameSource {
  const uint8_t* pixels = nullptr;
  Nv12ConversionParams params;
};

// Byte order within a pixel: ABGR has red first, ARGB has blue first.
Result<uint32_t> RedOffset(uint32_t pixel_format) {
  if (pixel_format == DRM_FORMAT_ARGB8888 ||
      pixel_format == DRM_FORMAT_XRGB8888) {
    return 2u;
  }
  CF_EXPECTF(pixel_format == DRM_FORMAT_ABGR8888 ||
                 pixel_format == DRM_FORMAT_XBGR8888,
             "Unsupported pixel format: {:#x}", pixel_format);
  return 0u;
}

// Reads the frame geometry and pixel order out of a native buffer.
Result<FrameSource> ReadFrameSource(const webrtc::VideoFrame& frame,
                                    uint32_t width, uint32_t height,
                                    VkExtent2D coded_extent) {
  const rtc::scoped_refptr<webrtc::VideoFrameBuffer> buffer =
      frame.video_frame_buffer();
  CF_EXPECTF(buffer->type() == webrtc::VideoFrameBuffer::Type::kNative,
             "Non-native buffer type: {}", static_cast<int>(buffer->type()));

  const AbgrBuffer* const rgba_buffer =
      static_cast<const AbgrBuffer*>(buffer.get());
  const uint32_t red_offset = CF_EXPECT(RedOffset(rgba_buffer->PixelFormat()));

  const uint8_t* const pixels = rgba_buffer->Data();
  CF_EXPECT(pixels != nullptr, "Frame has no pixel data");
  const size_t source_stride = static_cast<size_t>(rgba_buffer->Stride());
  // The converter takes the stride in pixels, so a row has to start on a pixel
  // boundary.
  CF_EXPECT_EQ(
      source_stride % kRgbaBytesPerPixel, 0u,
      "Frame stride " << source_stride << " is not a whole pixel count");

  return FrameSource{
      .pixels = pixels,
      .params =
          {
              .visible_width = width,
              .visible_height = height,
              .coded_width = coded_extent.width,
              .coded_height = coded_extent.height,
              .source_stride_pixels =
                  static_cast<uint32_t>(source_stride / kRgbaBytesPerPixel),
              .red_offset = red_offset,
          },
  };
}

}  // namespace

VulkanVideoEncoder::VulkanVideoEncoder(const VulkanEncoderConfig& config,
                                       const webrtc::SdpVideoFormat& format)
    : config_(config) {
  VLOG(1) << "Creating VulkanVideoEncoder (" << config_.implementation_name
          << ") for format: " << format.ToString();
}

VulkanVideoEncoder::~VulkanVideoEncoder() = default;

int32_t VulkanVideoEncoder::Release() {
  session_.reset();
  return WEBRTC_VIDEO_CODEC_OK;
}

int VulkanVideoEncoder::InitEncode(
    const webrtc::VideoCodec* codec_settings,
    const webrtc::VideoEncoder::Settings& settings) {
  session_.reset();

  VLOG(1) << "VulkanVideoEncoder::InitEncode (" << config_.implementation_name
          << ")";

  if (!codec_settings) {
    LOG(ERROR) << "InitEncode: codec_settings is null";
    return WEBRTC_VIDEO_CODEC_ERR_PARAMETER;
  }

  width_ = codec_settings->width;
  height_ = codec_settings->height;

  const Result<void> result = InitEncodeInner();
  if (!result.has_value()) {
    LOG(ERROR) << "Vulkan encoder initialization failed: " << result.error();
    return WEBRTC_VIDEO_CODEC_ERROR;
  }
  return WEBRTC_VIDEO_CODEC_OK;
}

Result<void> VulkanVideoEncoder::InitEncodeInner() {
  const VulkanAv1SessionConfig session_config = {
      .width = width_,
      .height = height_,
  };
  session_ = CF_EXPECT(VulkanAv1EncodeSession::Create(session_config));
  return {};
}

int32_t VulkanVideoEncoder::RegisterEncodeCompleteCallback(
    webrtc::EncodedImageCallback* callback) {
  callback_ = callback;
  return WEBRTC_VIDEO_CODEC_OK;
}

// The session encodes at a constant quantizer index, so the rates go unused.
void VulkanVideoEncoder::SetRates(const RateControlParameters& parameters) {}

int32_t VulkanVideoEncoder::Encode(
    const webrtc::VideoFrame& frame,
    const std::vector<webrtc::VideoFrameType>* frame_types) {
  if (session_ == nullptr || callback_ == nullptr) {
    return WEBRTC_VIDEO_CODEC_UNINITIALIZED;
  }

  const Result<void> result = EncodeInner(frame);
  if (!result.has_value()) {
    LOG(ERROR) << "Encode failed: " << result.error();
    return WEBRTC_VIDEO_CODEC_ERROR;
  }
  return WEBRTC_VIDEO_CODEC_OK;
}

Result<void> VulkanVideoEncoder::EncodeInner(const webrtc::VideoFrame& frame) {
  CF_EXPECT_EQ(static_cast<uint32_t>(frame.width()), width_,
               "Frame width changed since InitEncode");
  CF_EXPECT_EQ(static_cast<uint32_t>(frame.height()), height_,
               "Frame height changed since InitEncode");

  const FrameSource source = CF_EXPECT(
      ReadFrameSource(frame, width_, height_, session_->coded_extent()));
  const VulkanAv1EncodedFrame encoded =
      CF_EXPECT(session_->EncodeFrame(source.pixels, source.params));

  webrtc::EncodedImage encoded_image;
  encoded_image.SetEncodedData(webrtc::EncodedImageBuffer::Create(
      encoded.bitstream.data(), encoded.bitstream.size()));
  encoded_image._encodedWidth = width_;
  encoded_image._encodedHeight = height_;
  encoded_image.SetTimestamp(frame.timestamp());
  encoded_image.ntp_time_ms_ = frame.ntp_time_ms();
  encoded_image._frameType = webrtc::VideoFrameType::kVideoFrameKey;

  webrtc::CodecSpecificInfo codec_specific = {};
  codec_specific.codecType = config_.webrtc_codec_type;

  callback_->OnEncodedImage(encoded_image, &codec_specific);
  return {};
}

webrtc::VideoEncoder::EncoderInfo VulkanVideoEncoder::GetEncoderInfo() const {
  EncoderInfo info;
  info.supports_native_handle = true;
  info.implementation_name = config_.implementation_name;
  info.is_hardware_accelerated = true;

  info.preferred_pixel_formats = {webrtc::VideoFrameBuffer::Type::kNative};

  info.resolution_bitrate_limits.assign(
      config_.bitrate_limits,
      config_.bitrate_limits + config_.bitrate_limits_count);

  return info;
}

}  // namespace cuttlefish
