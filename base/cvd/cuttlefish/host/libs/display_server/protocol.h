/*
 * Copyright (C) 2026 The Android Open Source Project
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

namespace cuttlefish::display_server {

// Messages exchanged between cvd_display_server and its clients.
// This is a cuttlefish specific protocol, not based on an existing one.

inline constexpr uint32_t kFrameSlotsPerDisplay = 4;

// Frame info stored in a shared memory.
struct FrameInfo {
  uint32_t width;
  uint32_t height;
  uint32_t fourcc_format;
  uint32_t stride_bytes;
};

// Start of each display's shared memory object. It is followed by
// kFrameSlotsPerDisplay frame data slots of `slot_size` bytes each.
struct alignas(64) ShmHeader {
  uint64_t slot_size;
  FrameInfo frame;
};

// Byte offset of a slot's pixels in the shared memory object.
inline constexpr size_t SlotOffset(uint64_t slot_size, uint32_t slot) {
  return sizeof(ShmHeader) + slot * slot_size;
}

enum class MsgType : uint32_t {
  // A display was added. Used with display_number, width and height.
  kDisplayCreated = 1,
  // A display was removed. Used with display_number.
  kDisplayDestroyed = 2,
  // Shared memory info for display. Used with display_number and shm_name.
  kDisplayShm = 3,
  // A new frame is available. Indicates that it is FrameMsg.
  kFrame = 4,
};

// Sent by the server to the client for display changes.
struct DisplayMsg {
  MsgType type;
  uint32_t display_number;
  uint32_t width;
  uint32_t height;
  // Name of the shared memory object holding the display's frame slots.
  char shm_name[64];
};

// Sent by the server to the client for every new frame. The frame geometry is
// in the shared memory header.
struct FrameMsg {
  MsgType type;
  uint32_t display_number;
  // Slot number of the frame in the shared memory.
  uint32_t slot;
};

inline constexpr size_t kMaxMsgSize = 256;
static_assert(sizeof(DisplayMsg) <= kMaxMsgSize);
static_assert(sizeof(FrameMsg) <= kMaxMsgSize);

inline constexpr size_t kMsgAlignment = 8;
static_assert(alignof(DisplayMsg) <= kMsgAlignment);
static_assert(alignof(FrameMsg) <= kMsgAlignment);

}  // namespace cuttlefish::display_server
