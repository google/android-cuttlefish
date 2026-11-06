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

#include <stdint.h>

#include <utility>

#include "vulkan/vk_platform.h"
#include "vulkan/vulkan_core.h"

namespace cuttlefish {

// Owns a non-dispatchable Vulkan object created from a device and destroys it
// with the vkDestroy* or vkFree* entry point it was given. These handles are
// integers on 32-bit builds, so std::unique_ptr cannot hold them.
template <typename Handle>
class UniqueVkHandle {
 public:
  using DestroyFunction = void(VKAPI_PTR*)(VkDevice, Handle,
                                           const VkAllocationCallbacks*);

  UniqueVkHandle() = default;
  UniqueVkHandle(VkDevice device, Handle handle, DestroyFunction destroy)
      : device_(device), handle_(handle), destroy_(destroy) {}

  UniqueVkHandle(const UniqueVkHandle&) = delete;
  UniqueVkHandle& operator=(const UniqueVkHandle&) = delete;

  UniqueVkHandle(UniqueVkHandle&& other) noexcept
      : device_(other.device_),
        handle_(std::exchange(other.handle_, VK_NULL_HANDLE)),
        destroy_(other.destroy_) {}

  UniqueVkHandle& operator=(UniqueVkHandle&& other) noexcept {
    if (this != &other) {
      Reset();
      device_ = other.device_;
      handle_ = std::exchange(other.handle_, VK_NULL_HANDLE);
      destroy_ = other.destroy_;
    }
    return *this;
  }

  ~UniqueVkHandle() { Reset(); }

  Handle get() const { return handle_; }

 private:
  void Reset() {
    if (handle_ != VK_NULL_HANDLE) {
      destroy_(device_, handle_, nullptr);
      handle_ = VK_NULL_HANDLE;
    }
  }

  VkDevice device_ = VK_NULL_HANDLE;
  Handle handle_ = VK_NULL_HANDLE;
  DestroyFunction destroy_ = nullptr;
};

// Owns the host mapping of a device allocation and unmaps it on destruction.
class VulkanMemoryMapping {
 public:
  VulkanMemoryMapping() = default;
  VulkanMemoryMapping(VkDevice device, VkDeviceMemory memory, void* data,
                      PFN_vkUnmapMemory unmap)
      : device_(device), memory_(memory), data_(data), unmap_(unmap) {}

  VulkanMemoryMapping(const VulkanMemoryMapping&) = delete;
  VulkanMemoryMapping& operator=(const VulkanMemoryMapping&) = delete;

  VulkanMemoryMapping(VulkanMemoryMapping&& other) noexcept
      : device_(other.device_),
        memory_(other.memory_),
        data_(std::exchange(other.data_, nullptr)),
        unmap_(other.unmap_) {}

  VulkanMemoryMapping& operator=(VulkanMemoryMapping&& other) noexcept {
    if (this != &other) {
      Reset();
      device_ = other.device_;
      memory_ = other.memory_;
      data_ = std::exchange(other.data_, nullptr);
      unmap_ = other.unmap_;
    }
    return *this;
  }

  ~VulkanMemoryMapping() { Reset(); }

  uint8_t* data() const { return static_cast<uint8_t*>(data_); }

 private:
  void Reset() {
    if (data_ != nullptr) {
      unmap_(device_, memory_);
      data_ = nullptr;
    }
  }

  VkDevice device_ = VK_NULL_HANDLE;
  VkDeviceMemory memory_ = VK_NULL_HANDLE;
  void* data_ = nullptr;
  PFN_vkUnmapMemory unmap_ = nullptr;
};

}  // namespace cuttlefish
