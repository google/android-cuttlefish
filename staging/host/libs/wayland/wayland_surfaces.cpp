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

Surface* Surfaces::GetOrCreateSurface(std::uint32_t id) {
  std::unique_lock<std::mutex> lock(surfaces_mutex_);

  auto [it, inserted] = surfaces_.try_emplace(id, nullptr);

  std::unique_ptr<Surface>& surface_ptr = it->second;
  if (inserted) {
    surface_ptr.reset(new Surface(id, *this));
  }
  return surface_ptr.get();
}

void Surfaces::OnNextFrame(const FrameCallback& frame_callback) {
  // Wraps the given callback in a std::package_task that can be waited upon
  // for completion.
  Surfaces::FrameCallbackPackaged frame_callback_packaged(
      [&frame_callback](std::uint32_t display_number,
                        std::uint8_t* frame_pixels) {
        frame_callback(display_number, frame_pixels);
      });

  {
    std::unique_lock<std::mutex> lock(callback_mutex_);
    callback_.emplace(&frame_callback_packaged);
  }

  // Blocks until the frame_callback_packaged was called.
  frame_callback_packaged.get_future().get();
}

void Surfaces::HandleSurfaceFrame(std::uint32_t display_number,
                                  std::uint8_t* frame_bytes) {
  std::unique_lock<std::mutex> lock(callback_mutex_);
  if (callback_) {
    (*callback_.value())(display_number, frame_bytes);
    callback_.reset();
  }
}

}  // namespace wayland