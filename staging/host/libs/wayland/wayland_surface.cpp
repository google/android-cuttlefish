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

#include "host/libs/wayland/wayland_surface.h"

#include <android-base/logging.h>
#include <wayland-server-protocol.h>

#include "host/libs/wayland/wayland_surfaces.h"

namespace wayland {

Surface::Surface(Surfaces& surfaces) : surfaces_(surfaces) {}

void Surface::SetRegion(const Region& region) {
  std::unique_lock<std::mutex> lock(state_mutex_);
  state_.region = region;
}

void Surface::Attach(struct wl_resource* buffer) {
  std::unique_lock<std::mutex> lock(state_mutex_);
  state_.pending_buffer = buffer;
}

void Surface::Commit() {
  std::unique_lock<std::mutex> lock(state_mutex_);
  state_.current_buffer = state_.pending_buffer;
  state_.pending_buffer = nullptr;

  if (state_.current_buffer == nullptr) {
    return;
  }

  if (state_.virtio_gpu_metadata_.scanout_id.has_value()) {
    const uint32_t display_number = *state_.virtio_gpu_metadata_.scanout_id;

    struct wl_shm_buffer* shm_buffer = wl_shm_buffer_get(state_.current_buffer);
    CHECK(shm_buffer != nullptr);

    wl_shm_buffer_begin_access(shm_buffer);

    const int32_t buffer_w = wl_shm_buffer_get_width(shm_buffer);
    CHECK(buffer_w == state_.region.w);
    const int32_t buffer_h = wl_shm_buffer_get_height(shm_buffer);
    CHECK(buffer_h == state_.region.h);
    const int32_t buffer_stride_bytes = wl_shm_buffer_get_stride(shm_buffer);

    uint8_t* buffer_pixels =
        reinterpret_cast<uint8_t*>(wl_shm_buffer_get_data(shm_buffer));

    surfaces_.HandleSurfaceFrame(display_number, buffer_w, buffer_h,
                                 buffer_stride_bytes, buffer_pixels);

    wl_shm_buffer_end_access(shm_buffer);
  }

  wl_buffer_send_release(state_.current_buffer);
  wl_client_flush(wl_resource_get_client(state_.current_buffer));

  state_.current_buffer = nullptr;
  state_.current_frame_number++;
}

void Surface::SetVirtioGpuScanoutId(uint32_t scanout_id) {
  std::unique_lock<std::mutex> lock(state_mutex_);
  state_.virtio_gpu_metadata_.scanout_id = scanout_id;
}

}  // namespace wayland