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

#include <stddef.h>
#include <stdint.h>

#include "android-base/unique_fd.h"
#include "wayland-server-core.h"

namespace wayland {

struct DmabufBuffer {
  android::base::unique_fd fd;
  int32_t width = 0;
  int32_t height = 0;
  uint32_t format = 0;
  uint32_t flags = 0;
  uint32_t offset = 0;
  uint32_t stride = 0;
  uint64_t modifier = 0;
  uint8_t* mapped_ptr = nullptr;
  size_t mapped_size = 0;

  ~DmabufBuffer();
};

// Returns the DmabufBuffer associated with the given wl_buffer resource,
// or nullptr if the resource is not a dmabuf wl_buffer.
DmabufBuffer* GetDmabufBuffer(wl_resource* buffer);

void DmabufBufferBeginAccess(DmabufBuffer* buffer);
void DmabufBufferEndAccess(DmabufBuffer* buffer);

// Binds the dmabuf interface to the given wayland server.
void BindDmabufInterface(wl_display* display);

}  // namespace wayland
