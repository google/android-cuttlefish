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

#include <type_traits>

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
static_assert(std::is_trivially_copyable_v<ShmHeader>);
static_assert(std::is_standard_layout_v<ShmHeader>);

// Byte offset of a slot's pixels in the shared memory object.
inline constexpr size_t SlotOffset(uint64_t slot_size, uint32_t slot) {
  return sizeof(ShmHeader) + slot * slot_size;
}

enum class MsgType : uint32_t {
  // A display was added. Payload is DisplayCreatedMsg.
  kDisplayCreated = 1,
  // A display was removed. Payload is DisplayDestroyedMsg.
  kDisplayDestroyed = 2,
  // Shared memory info for a display. Payload is DisplayShmMsg.
  kDisplayShm = 3,
  // A new frame is available. Payload is FrameMsg.
  kFrame = 4,
};

struct DisplayCreatedMsg {
  uint32_t display_number;
  uint32_t width;
  uint32_t height;
};

struct DisplayDestroyedMsg {
  uint32_t display_number;
};

struct DisplayShmMsg {
  uint32_t display_number;
  // Name of the shared memory object holding the display's frame slots.
  char shm_name[64];
};

// The frame geometry is in the shared memory header.
struct FrameMsg {
  uint32_t display_number;
  // Slot number of the frame in the shared memory.
  uint32_t slot;
};

// Sent by the server to the client.
struct Message {
  MsgType type;
  union {
    DisplayCreatedMsg created;
    DisplayDestroyedMsg destroyed;
    DisplayShmMsg shm;
    FrameMsg frame;
  };
};
static_assert(std::is_trivially_copyable_v<Message>);
static_assert(std::is_standard_layout_v<Message>);

inline constexpr size_t kMaxMsgSize = 256;
static_assert(sizeof(Message) <= kMaxMsgSize);

inline constexpr size_t kMsgAlignment = 8;
static_assert(alignof(Message) <= kMsgAlignment);

}  // namespace cuttlefish::display_server
