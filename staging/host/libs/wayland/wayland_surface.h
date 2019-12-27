/*
 * Copyright (C) 2019 The Android Open Source Project
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

#include <optional>
#include <stdint.h>
#include <thread>
#include <future>

#include <wayland-server-core.h>

namespace wayland {

// Tracks the buffer associated with a Wayland surface.
class Surface {
 public:
  Surface() = default;
  virtual ~Surface() = default;

  Surface(const Surface& rhs) = delete;
  Surface& operator=(const Surface& rhs) = delete;

  Surface(Surface&& rhs) = delete;
  Surface& operator=(Surface&& rhs) = delete;

  struct Region {
    int32_t x;
    int32_t y;
    int32_t w;
    int32_t h;
  };

  void SetRegion(const Region& region);

  // Sets the buffer of the pending frame.
  void Attach(struct wl_resource* buffer);

  // Commits the pending frame state.
  void Commit();

  using FrameCallbackPackaged =
    std::packaged_task<void(std::uint32_t /*frame_number*/,
                            std::uint8_t* /*frame_pixels*/)>;

  // Registers a callback that should be invoked on the first frame after
  // the given frame number.
  void OnFrameAfter(uint32_t frame_number,
                    FrameCallbackPackaged frame_callback);

 private:
  struct State {
    uint32_t current_frame_number = 0;

    // The buffer for the current committed frame.
    struct wl_resource* current_buffer = nullptr;

    // The buffer for the next frame.
    struct wl_resource* pending_buffer = nullptr;

    // The buffers expected dimensions.
    Region region;
  };

  std::mutex state_mutex_;
  State state_;

  struct PendingFrameCallback {
    uint32_t frame_number;

    FrameCallbackPackaged frame_callback;
  };

  std::mutex callback_mutex_;
  std::optional<PendingFrameCallback> callback_;
};

}  // namespace wayland