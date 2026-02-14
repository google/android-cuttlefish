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

#include <stdint.h>

#include <atomic>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>

#include "cuttlefish/common/libs/fs/shared_fd.h"
#include "cuttlefish/result/result.h"

namespace cuttlefish {

// This header is allocated / placed at start of IPC ringbuffer. Intention of
// elements here is to allow to compute the valid read/write address for
// current frame from an external process.
struct DisplayRingBufferHeader {
  volatile uint32_t display_width_;
  volatile uint32_t display_height_;
  volatile uint32_t bpp_;
  std::atomic<uint32_t> last_valid_frame_index_;

  void set(uint32_t w, uint32_t h, uint32_t bpp, uint32_t index);
};

class DisplayRingBuffer {
 public:
  ~DisplayRingBuffer();
  static Result<std::unique_ptr<DisplayRingBuffer>> Create(
      const std::string& name, int size);
  // Allowing optional in the case the buffer doesn't yet exist.
  static std::optional<std::unique_ptr<DisplayRingBuffer>> ShmemGet(
      const std::string& name, int size);

  void* GetAddress();

  uint8_t* WriteNextFrame(uint8_t* frame_data, int size);
  uint8_t* CurrentFrame();
  uint8_t* ComputeFrameAddressForIndex(uint32_t index);

 private:
  DisplayRingBuffer(void* addr, std::string name, bool owned, ScopedMMap shm);

  DisplayRingBufferHeader* header_;
  void* addr_;
  std::string name_;
  bool owned_;
  ScopedMMap shm_;
};

class DisplayRingBufferManager {
 public:
  DisplayRingBufferManager(int vm_index, std::string group_uuid);
  Result<void> CreateLocalDisplayBuffer(int vm_index, int display_index,
                                        int display_width, int display_height);
  uint8_t* WriteFrame(int vm_index, int display_index, uint8_t* frame_data,
                      int size);
  uint8_t* ReadFrame(int vm_index, int display_index, int frame_width,
                     int frame_height);

 private:
  std::string MakeLayerName(int display_index, int vm_index = -1);
  int local_group_index_;  // Index of the current process in the cluster of VMs
  std::string group_uuid_;  // Unique identifier for entire VM cluster
  // All IPC buffers are cached here for speed, to prevent OS from
  // continually remapping RAM every read/write request.
  std::map<std::pair<int, int>, std::unique_ptr<DisplayRingBuffer>>
      display_buffer_cache_;
};

}  // namespace cuttlefish
