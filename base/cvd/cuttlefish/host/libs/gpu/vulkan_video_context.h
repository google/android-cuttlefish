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

#include <memory>
#include <mutex>
#include <optional>
#include <vector>

#include "vulkan/vulkan_core.h"

#include "cuttlefish/host/libs/gpu/vulkan_av1_capabilities.h"
#include "cuttlefish/host/libs/gpu/vulkan_loader.h"
#include "cuttlefish/result/result.h"

namespace cuttlefish {

// Returns true if `extensions` holds every device extension AV1 encoding
// needs.
bool HasRequiredVideoEncodeExtensions(
    const std::vector<VkExtensionProperties>& extensions);

// Returns the first queue family that can encode video.
std::optional<uint32_t> FindVideoEncodeQueueFamily(
    const std::vector<VkQueueFamilyProperties>& families);

// Returns the first queue family that can run compute shaders. The color
// conversion shader and the frame copies need one, and the encode family need
// not support compute.
std::optional<uint32_t> FindComputeQueueFamily(
    const std::vector<VkQueueFamilyProperties>& families);

// Returns true if this host can encode AV1 through Vulkan Video.
// Thread-safe; probes once per process and caches the result.
bool IsVulkanAv1EncodeSupported();

// A device queue and the family it belongs to.
struct VulkanQueue {
  uint32_t family = 0;
  VkQueue queue = VK_NULL_HANDLE;
};

// Vulkan instance and device holding a video encode queue and a compute
// queue. Get() shares one context among the callers that hold it and creates
// a new one once all have released it. Thread-safe.
class VulkanVideoContext {
 public:
  // Returns the shared context, creating it if no caller holds one. Fails if
  // no device on this host can encode AV1 with Vulkan.
  static Result<std::shared_ptr<VulkanVideoContext>> Get();

  VulkanVideoContext(const VulkanVideoContext&) = delete;
  VulkanVideoContext& operator=(const VulkanVideoContext&) = delete;

  VkPhysicalDevice physical_device() const { return physical_device_; }
  VkDevice device() const { return device_.get(); }
  VkQueue encode_queue() const { return encode_queue_.queue; }
  uint32_t encode_queue_family() const { return encode_queue_.family; }
  // Where the compute and encode families are the same, this is the encode
  // queue.
  VkQueue compute_queue() const { return compute_queue_.queue; }
  uint32_t compute_queue_family() const { return compute_queue_.family; }

  const VulkanAv1EncodeCapabilities& av1_capabilities() const {
    return av1_capabilities_;
  }

  // Returns a memory type from `type_bits` that has all of `properties`, or
  // nullopt when the device has none.
  std::optional<uint32_t> FindMemoryType(
      uint32_t type_bits, VkMemoryPropertyFlags properties) const;

  const VkPhysicalDeviceMemoryProperties& memory_properties() const {
    return memory_properties_;
  }
  const VulkanInstanceFunctions& instance_functions() const {
    return instance_funcs_;
  }
  const VulkanDeviceFunctions& device_functions() const {
    return device_funcs_;
  }

  // Serialises submissions to the encode queue, which every encoder in the
  // process shares. Vulkan requires queue submissions to be externally
  // synchronized.
  std::mutex& encode_queue_mutex() { return encode_queue_mutex_; }

  // The same for the compute queue, which every encoder in the process shares
  // for color conversion and frame copies. Where the compute queue is the
  // encode queue, one mutex guards it.
  std::mutex& compute_queue_mutex() {
    return compute_queue_.queue == encode_queue_.queue ? encode_queue_mutex_
                                                       : compute_queue_mutex_;
  }

 private:
  static Result<std::shared_ptr<VulkanVideoContext>> Create();

  VulkanVideoContext(UniqueVkInstance instance,
                     const VulkanInstanceFunctions& instance_funcs,
                     VkPhysicalDevice physical_device,
                     VulkanAv1EncodeCapabilities av1_capabilities,
                     UniqueVkDevice device,
                     const VulkanDeviceFunctions& device_funcs,
                     VulkanQueue encode_queue, VulkanQueue compute_queue);

  // Declared before the device so that the device is destroyed first.
  UniqueVkInstance instance_;
  VulkanInstanceFunctions instance_funcs_;
  VkPhysicalDevice physical_device_ = VK_NULL_HANDLE;
  VkPhysicalDeviceMemoryProperties memory_properties_ = {};
  VulkanAv1EncodeCapabilities av1_capabilities_;
  UniqueVkDevice device_;
  VulkanDeviceFunctions device_funcs_;
  VulkanQueue encode_queue_;
  VulkanQueue compute_queue_;
  std::mutex encode_queue_mutex_;
  std::mutex compute_queue_mutex_;
};

}  // namespace cuttlefish
