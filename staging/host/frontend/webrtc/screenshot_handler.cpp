/*
 * Copyright (C) 2024 The Android Open Source Project
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

#include "host/frontend/webrtc/screenshot_handler.h"

#include <filesystem>
#include <fstream>

#include <SkData.h>
#include <SkImage.h>
#include <SkJpegEncoder.h>
#include <SkPngEncoder.h>
#include <SkRefCnt.h>
#include <SkStream.h>
#include <SkWebpEncoder.h>
#include <libyuv.h>

namespace cuttlefish {
namespace {

Result<sk_sp<SkImage>> GetSkImage(
    const webrtc_streaming::VideoFrameBuffer& frame) {
  const int w = frame.width();
  const int h = frame.height();

  sk_sp<SkData> rgba_data = SkData::MakeUninitialized(w * h * 4);
  const int rgba_stride = w * 4;

  int ret = libyuv::I420ToABGR(
      frame.DataY(), frame.StrideY(),                                       //
      frame.DataU(), frame.StrideU(),                                       //
      frame.DataV(), frame.StrideV(),                                       //
      reinterpret_cast<uint8_t*>(rgba_data->writable_data()), rgba_stride,  //
      w, h);
  CF_EXPECT_EQ(ret, 0, "Failed to convert input frame to RGBA.");

  const SkImageInfo& image_info =
      SkImageInfo::Make(w, h, kRGBA_8888_SkColorType, kOpaque_SkAlphaType);

  sk_sp<SkImage> image =
      SkImages::RasterFromData(image_info, rgba_data, rgba_stride);
  CF_EXPECT(image != nullptr, "Failed to raster RGBA data.");

  return image;
}

}  // namespace

Result<void> ScreenshotHandler::Screenshot(std::uint32_t display_number,
                                           const std::string& screenshot_path) {
  SharedFrameFuture frame_future;
  {
    std::lock_guard<std::mutex> lock(pending_screenshot_displays_mutex_);

    auto [it, inserted] = pending_screenshot_displays_.emplace(
        display_number, SharedFramePromise{});
    if (!inserted) {
      return CF_ERRF("Screenshot already pending for display {}",
                     display_number);
    }

    frame_future = it->second.get_future().share();
  }

  static constexpr const int kScreenshotTimeoutSeconds = 5;
  auto result =
      frame_future.wait_for(std::chrono::seconds(kScreenshotTimeoutSeconds));
  CF_EXPECT(result == std::future_status::ready,
            "Failed to get screenshot from webrtc display handler within "
                << kScreenshotTimeoutSeconds << " seconds.");

  SharedFrame frame = frame_future.get();

  sk_sp<SkImage> screenshot_image =
      CF_EXPECT(GetSkImage(*frame), "Failed to get skia image from raw frame.");

  sk_sp<SkData> screenshot_data;
  if (screenshot_path.ends_with(".jpg")) {
    screenshot_data =
        SkJpegEncoder::Encode(nullptr, screenshot_image.get(), {});
    CF_EXPECT(screenshot_data != nullptr, "Failed to encode to JPEG.");
  } else if (screenshot_path.ends_with(".png")) {
    screenshot_data = SkPngEncoder::Encode(nullptr, screenshot_image.get(), {});
    CF_EXPECT(screenshot_data != nullptr, "Failed to encode to PNG.");
  } else if (screenshot_path.ends_with(".webp")) {
    screenshot_data =
        SkWebpEncoder::Encode(nullptr, screenshot_image.get(), {});
    CF_EXPECT(screenshot_data != nullptr, "Failed to encode to WEBP.");
  } else {
    return CF_ERR("Unsupport file format: " << screenshot_path);
  }

  SkFILEWStream screenshot_file(screenshot_path.c_str());
  CF_EXPECT(screenshot_file.isValid(),
            "Failed to open " << screenshot_path << " for writing.");

  CF_EXPECT(
      screenshot_file.write(screenshot_data->data(), screenshot_data->size()),
      "Failed to fully write png content to " << screenshot_path << ".");

  return {};
}

void ScreenshotHandler::OnFrame(std::uint32_t display_number,
                                SharedFrame& frame) {
  std::lock_guard<std::mutex> lock(pending_screenshot_displays_mutex_);

  auto pending_screenshot_it =
      pending_screenshot_displays_.find(display_number);
  if (pending_screenshot_it == pending_screenshot_displays_.end()) {
    return;
  }
  SharedFramePromise& frame_promise = pending_screenshot_it->second;

  frame_promise.set_value(frame);

  pending_screenshot_displays_.erase(pending_screenshot_it);
}

}  // namespace cuttlefish
