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

#pragma once

#include <future>
#include <mutex>
#include <unordered_set>

#include <fmt/format.h>

#include "common/libs/utils/result.h"
#include "host/frontend/webrtc/libdevice/video_frame_buffer.h"

namespace cuttlefish {

class ScreenshotHandler {
 public:
  ScreenshotHandler() = default;
  ~ScreenshotHandler() = default;

  using SharedFrame = std::shared_ptr<webrtc_streaming::VideoFrameBuffer>;
  using SharedFrameFuture = std::shared_future<SharedFrame>;
  using SharedFramePromise = std::promise<SharedFrame>;

  Result<void> Screenshot(std::uint32_t display_number,
                          const std::string& screenshot_path);

  void OnFrame(std::uint32_t display_number, SharedFrame& buffer);

 private:
  std::mutex pending_screenshot_displays_mutex_;
  // Promises used to share a frame for a given display from the display handler
  // thread to the snapshot thread for processing.
  std::unordered_map<std::uint32_t, SharedFramePromise>
      pending_screenshot_displays_;
};

}  // namespace cuttlefish
