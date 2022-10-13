/*
 * Copyright (C) 2020 The Android Open Source Project
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

#include "host/libs/wayland/wayland_surfaces.h"

#include <android-base/logging.h>
#include <wayland-server-protocol.h>

#include "host/libs/wayland/wayland_surface.h"

namespace wayland {

void Surfaces::SetFrameCallback(FrameCallback callback) {
  std::unique_lock<std::mutex> lock(callback_mutex_);
  callback_.emplace(std::move(callback));
}

void Surfaces::SetDisplayEventCallback(DisplayEventCallback callback) {
  std::unique_lock<std::mutex> lock(callback_mutex_);
  event_callback_.emplace(std::move(callback));
}

void Surfaces::HandleSurfaceFrame(std::uint32_t display_number,
                                  std::uint32_t frame_width,
                                  std::uint32_t frame_height,
                                  std::uint32_t frame_stride_bytes,
                                  std::uint8_t* frame_bytes) {
  std::unique_lock<std::mutex> lock(callback_mutex_);
  if (callback_) {
    (callback_.value())(display_number, frame_width, frame_height,
                        frame_stride_bytes, frame_bytes);
  }
}

void Surfaces::HandleSurfaceCreated(std::uint32_t display_number,
                                    std::uint32_t display_width,
                                    std::uint32_t display_height) {
  const DisplayEvent event{DisplayCreatedEvent{
      .display_number = display_number,
      .display_width = display_width,
      .display_height = display_height,
  }};

  std::unique_lock<std::mutex> lock(callback_mutex_);
  if (event_callback_) {
    (event_callback_.value())(event);
  }
}

void Surfaces::HandleSurfaceDestroyed(std::uint32_t display_number) {
  const DisplayEvent event{DisplayDestroyedEvent{
      .display_number = display_number,
  }};

  std::unique_lock<std::mutex> lock(callback_mutex_);
  if (event_callback_) {
    (event_callback_.value())(event);
  }
}

}  // namespace wayland