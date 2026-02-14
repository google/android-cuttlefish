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

#include "cuttlefish/host/libs/screen_connector/ring_buffer_manager.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <memory>
#include <optional>
#include <string>

#include <fmt/format.h>

#include "cuttlefish/common/libs/fs/shared_fd.h"

namespace cuttlefish {

namespace {
constexpr int kNumberOfRingBufferFrames = 3;
inline int RingBufferMemorySize(int w, int h) {
  return sizeof(DisplayRingBufferHeader) +
         ((w * h * 4) * kNumberOfRingBufferFrames);
}
}  // namespace

void* DisplayRingBuffer::GetAddress() { return addr_; };

Result<std::unique_ptr<DisplayRingBuffer>> DisplayRingBuffer::Create(
    const std::string& name, int size) {
  void* addr = nullptr;

  SharedFD sfd =
      SharedFD::ShmOpen(name, O_RDWR | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR);

  CF_EXPECTF(sfd->IsOpen(), "Display buffer create failed {}", sfd->StrError());

  sfd->Truncate(size);

  ScopedMMap smm = sfd->MMap(NULL, size, PROT_WRITE, MAP_SHARED, 0);
  addr = smm.get();

  return std::unique_ptr<DisplayRingBuffer>(
      new DisplayRingBuffer(addr, name, true, std::move(smm)));
}

DisplayRingBuffer::~DisplayRingBuffer() {
  // Only unlink if we are the owner of the buffer.
  if (owned_) {
    shm_unlink(name_.c_str());
  }
}

// Allowing optional in the case the buffer doesn't yet exist.
std::optional<std::unique_ptr<DisplayRingBuffer>> DisplayRingBuffer::ShmemGet(
    const std::string& name, int size) {
  void* addr = nullptr;
  SharedFD sfd = SharedFD::ShmOpen(name, O_RDWR, S_IRUSR | S_IWUSR);
  if (!sfd->IsOpen()) {
    return std::nullopt;
  }
  ScopedMMap smm = sfd->MMap(NULL, size, PROT_WRITE, MAP_SHARED, 0);
  addr = smm.get();

  if (!addr) {
    return std::nullopt;
  }

  return std::unique_ptr<DisplayRingBuffer>(
      new DisplayRingBuffer(addr, name, false, std::move(smm)));
}

DisplayRingBuffer::DisplayRingBuffer(void* addr, std::string name, bool owned,
                                     ScopedMMap shm)
    : addr_(addr), name_(std::move(name)), owned_(owned), shm_(std::move(shm)) {
  header_ = (DisplayRingBufferHeader*)addr;
}

uint8_t* DisplayRingBuffer::WriteNextFrame(uint8_t* frame_data, int size) {
  int new_frame_index =
      (header_->last_valid_frame_index_ + 1) % kNumberOfRingBufferFrames;

  uint8_t* frame_memory_address = ComputeFrameAddressForIndex(new_frame_index);
  memcpy(frame_memory_address, frame_data, size);

  header_->last_valid_frame_index_ = new_frame_index;
  return frame_memory_address;
}

uint8_t* DisplayRingBuffer::CurrentFrame() {
  return ComputeFrameAddressForIndex(header_->last_valid_frame_index_);
}

uint8_t* DisplayRingBuffer::ComputeFrameAddressForIndex(uint32_t index) {
  int frame_memory_index = (index * (header_->display_width_ *
                                     header_->display_height_ * header_->bpp_));
  return ((uint8_t*)addr_) + sizeof(DisplayRingBufferHeader) +
         frame_memory_index;
}

void DisplayRingBufferHeader::set(uint32_t w, uint32_t h, uint32_t bpp,
                                  uint32_t index) {
  display_width_ = w;
  display_height_ = h;
  bpp_ = bpp;
  last_valid_frame_index_.store(index);
}

DisplayRingBufferManager::DisplayRingBufferManager(int vm_index,
                                                   std::string group_uuid)
    : local_group_index_(vm_index), group_uuid_(group_uuid) {}

Result<void> DisplayRingBufferManager::CreateLocalDisplayBuffer(
    int vm_index, int display_index, int display_width, int display_height) {
  auto buffer_key = std::make_pair(vm_index, display_index);

  if (!display_buffer_cache_.count(buffer_key)) {
    std::string shmem_name = MakeLayerName(display_index);

    auto shm_buffer = CF_EXPECT(DisplayRingBuffer::Create(
        shmem_name, RingBufferMemorySize(display_width, display_height)));
    uint8_t* shmem_local_display = (uint8_t*)shm_buffer->GetAddress();

    // Here we coerce the IPC buffer into having a header with metadata
    // containing DisplayRingBufferHeader struct.  Then copy the values over
    // so that the metadata is initialized correctly. This allows any process
    // to remotely understand the ringbuffer state properly, to obtain the size
    // and compute valid frame addresses for reading / writing frame data.
    DisplayRingBufferHeader* dbi =
        (DisplayRingBufferHeader*)shmem_local_display;
    dbi->set(display_width, display_height, 4, 0);

    display_buffer_cache_[buffer_key] = std::move(shm_buffer);
  }
  return {};
}

uint8_t* DisplayRingBufferManager::WriteFrame(int vm_index, int display_index,
                                              uint8_t* frame_data, int size) {
  auto buffer_key = std::make_pair(vm_index, display_index);
  if (display_buffer_cache_.count(buffer_key)) {
    return display_buffer_cache_[buffer_key]->WriteNextFrame(frame_data, size);
  }
  // It's possible to request a write to buffer that doesn't yet exist.
  return nullptr;
}

uint8_t* DisplayRingBufferManager::ReadFrame(int vm_index, int display_index,
                                             int frame_width,
                                             int frame_height) {
  auto buffer_key = std::make_pair(vm_index, display_index);

  // If this buffer was read successfully in the past, that valid pointer is
  // returned from the cache
  if (!display_buffer_cache_.count(buffer_key)) {
    // Since no cache found, next step is to request from OS to map a new IPC
    // buffer. It may not yet exist so we want this method to only cache if it
    // is a non-null pointer, to retrigger this logic continually every request.
    // Once the buffer exists the pointer would become non-null

    std::string shmem_name = MakeLayerName(display_index, vm_index);
    std::optional<std::unique_ptr<DisplayRingBuffer>> shmem_buffer =
        DisplayRingBuffer::ShmemGet(
            shmem_name.c_str(),
            RingBufferMemorySize(frame_width, frame_height));

    if (shmem_buffer.has_value() && shmem_buffer.value()->GetAddress()) {
      display_buffer_cache_[buffer_key] = std::move(shmem_buffer.value());
    } else {
      return nullptr;
    }
  }

  return display_buffer_cache_[buffer_key]->CurrentFrame();
}

std::string DisplayRingBufferManager::MakeLayerName(int display_index,
                                                    int vm_index) {
  if (vm_index == -1) {
    vm_index = local_group_index_;
  }
  return fmt::format("/cf_shmem_display_{}_{}_{}", vm_index, display_index,
                     group_uuid_);
}

}  // end namespace cuttlefish
