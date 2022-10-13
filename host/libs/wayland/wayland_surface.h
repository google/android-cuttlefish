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

#include <stdint.h>
#include <mutex>
#include <optional>

#include <wayland-server-core.h>

namespace wayland {

class Surfaces;

// Tracks the buffer associated with a Wayland surface.
class Surface {
 public:
  Surface(Surfaces& surfaces);
  virtual ~Surface();

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

  void SetVirtioGpuScanoutId(uint32_t scanout);

 private:
  Surfaces& surfaces_;

  struct VirtioGpuMetadata {
    std::optional<uint32_t> scanout_id;
  };

  struct State {
    uint32_t current_frame_number = 0;

    // The buffer for the current committed frame.
    struct wl_resource* current_buffer = nullptr;

    // The buffer for the next frame.
    struct wl_resource* pending_buffer = nullptr;

    // The buffers expected dimensions.
    Region region;

    VirtioGpuMetadata virtio_gpu_metadata_;

    bool has_notified_surface_create = false;
  };

  std::mutex state_mutex_;
  State state_;
};

}  // namespace wayland
