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

#include <android-base/file.h>
#include <webp/encode.h>

namespace cuttlefish {

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

  // The webp interface seems to only have non-const input. It does not look
  // like encode modifies the input but that does not appear to be guaranteed
  // anywhere. Make a copy to be safe.
  std::vector<uint8_t> y_copy(frame->DataY(),
                              frame->DataY() + frame->DataSizeY());
  std::vector<uint8_t> u_copy(frame->DataU(),
                              frame->DataU() + frame->DataSizeU());
  std::vector<uint8_t> v_copy(frame->DataV(),
                              frame->DataV() + frame->DataSizeV());

  WebPMemoryWriter writer;
  WebPMemoryWriterInit(&writer);

  WebPPicture picture;
  CF_EXPECT_NE(WebPPictureInit(&picture), 0,
               "Failed to initialize webp picture.");

  picture.width = frame->width();
  picture.height = frame->height();
  picture.use_argb = 0;  // `frame` is YUV420
  picture.colorspace = WEBP_YUV420;
  picture.y = y_copy.data();
  picture.u = u_copy.data();
  picture.v = v_copy.data();
  picture.y_stride = frame->StrideY();
  picture.uv_stride = frame->StrideU();
  picture.a = nullptr;
  picture.a_stride = 0;
  picture.writer = WebPMemoryWrite;
  picture.custom_ptr = &writer;

  WebPConfig config;
  CF_EXPECT_NE(WebPConfigPreset(&config, WEBP_PRESET_DEFAULT, 100), 0,
               "Failed to initialize webp config.");

  CF_EXPECT_NE(WebPEncode(&config, &picture), 0,
               "Failed to encode webp picture.");

  WebPPictureFree(&picture);

  const std::string screenshot_content(
      reinterpret_cast<const char*>(writer.mem), writer.size);
  android::base::WriteStringToFile(screenshot_content, screenshot_path);

  WebPMemoryWriterClear(&writer);

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
