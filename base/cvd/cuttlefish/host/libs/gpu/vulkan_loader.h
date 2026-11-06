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

#include <memory>

#include "vulkan/vulkan_core.h"

#include "cuttlefish/result/result.h"

namespace cuttlefish {

// Vulkan entry points that need no VkInstance, resolved from libvulkan.so.1
// with dlsym and vkGetInstanceProcAddr.
struct VulkanFunctions {
  PFN_vkGetInstanceProcAddr vkGetInstanceProcAddr = nullptr;
  PFN_vkCreateInstance vkCreateInstance = nullptr;
};

// Instance level entry points, resolved with vkGetInstanceProcAddr. Every one
// is required, so a loader or driver without one fails the load.
struct VulkanInstanceFunctions {
  PFN_vkEnumeratePhysicalDevices vkEnumeratePhysicalDevices = nullptr;
  PFN_vkGetPhysicalDeviceProperties vkGetPhysicalDeviceProperties = nullptr;
  PFN_vkGetPhysicalDeviceFeatures2 vkGetPhysicalDeviceFeatures2 = nullptr;
  PFN_vkGetPhysicalDeviceMemoryProperties vkGetPhysicalDeviceMemoryProperties =
      nullptr;
  PFN_vkGetPhysicalDeviceQueueFamilyProperties
      vkGetPhysicalDeviceQueueFamilyProperties = nullptr;
  PFN_vkEnumerateDeviceExtensionProperties
      vkEnumerateDeviceExtensionProperties = nullptr;
  PFN_vkGetPhysicalDeviceVideoCapabilitiesKHR
      vkGetPhysicalDeviceVideoCapabilitiesKHR = nullptr;
  PFN_vkGetPhysicalDeviceVideoFormatPropertiesKHR
      vkGetPhysicalDeviceVideoFormatPropertiesKHR = nullptr;
  PFN_vkCreateDevice vkCreateDevice = nullptr;
  PFN_vkGetDeviceProcAddr vkGetDeviceProcAddr = nullptr;
};

// Device level entry points, resolved with vkGetDeviceProcAddr. Every one is
// required, so a device whose driver lacks one is not used.
struct VulkanDeviceFunctions {
  PFN_vkDeviceWaitIdle vkDeviceWaitIdle = nullptr;
  PFN_vkGetDeviceQueue vkGetDeviceQueue = nullptr;
  PFN_vkQueueSubmit2 vkQueueSubmit2 = nullptr;
  PFN_vkAllocateMemory vkAllocateMemory = nullptr;
  PFN_vkFreeMemory vkFreeMemory = nullptr;
  PFN_vkMapMemory vkMapMemory = nullptr;
  PFN_vkUnmapMemory vkUnmapMemory = nullptr;
  PFN_vkFlushMappedMemoryRanges vkFlushMappedMemoryRanges = nullptr;
  PFN_vkInvalidateMappedMemoryRanges vkInvalidateMappedMemoryRanges = nullptr;
  PFN_vkCreateBuffer vkCreateBuffer = nullptr;
  PFN_vkDestroyBuffer vkDestroyBuffer = nullptr;
  PFN_vkGetBufferMemoryRequirements vkGetBufferMemoryRequirements = nullptr;
  PFN_vkBindBufferMemory vkBindBufferMemory = nullptr;
  PFN_vkCreateImage vkCreateImage = nullptr;
  PFN_vkDestroyImage vkDestroyImage = nullptr;
  PFN_vkGetImageMemoryRequirements vkGetImageMemoryRequirements = nullptr;
  PFN_vkBindImageMemory vkBindImageMemory = nullptr;
  PFN_vkCreateImageView vkCreateImageView = nullptr;
  PFN_vkDestroyImageView vkDestroyImageView = nullptr;
  PFN_vkCreateCommandPool vkCreateCommandPool = nullptr;
  PFN_vkDestroyCommandPool vkDestroyCommandPool = nullptr;
  PFN_vkAllocateCommandBuffers vkAllocateCommandBuffers = nullptr;
  PFN_vkBeginCommandBuffer vkBeginCommandBuffer = nullptr;
  PFN_vkEndCommandBuffer vkEndCommandBuffer = nullptr;
  PFN_vkResetCommandBuffer vkResetCommandBuffer = nullptr;
  PFN_vkCreateFence vkCreateFence = nullptr;
  PFN_vkDestroyFence vkDestroyFence = nullptr;
  PFN_vkWaitForFences vkWaitForFences = nullptr;
  PFN_vkResetFences vkResetFences = nullptr;
  PFN_vkCreateSemaphore vkCreateSemaphore = nullptr;
  PFN_vkDestroySemaphore vkDestroySemaphore = nullptr;
  PFN_vkCreateQueryPool vkCreateQueryPool = nullptr;
  PFN_vkDestroyQueryPool vkDestroyQueryPool = nullptr;
  PFN_vkGetQueryPoolResults vkGetQueryPoolResults = nullptr;
  PFN_vkCmdResetQueryPool vkCmdResetQueryPool = nullptr;
  PFN_vkCmdBeginQuery vkCmdBeginQuery = nullptr;
  PFN_vkCmdEndQuery vkCmdEndQuery = nullptr;
  PFN_vkCmdCopyBufferToImage vkCmdCopyBufferToImage = nullptr;
  PFN_vkCmdPipelineBarrier2 vkCmdPipelineBarrier2 = nullptr;
  PFN_vkCreateVideoSessionKHR vkCreateVideoSessionKHR = nullptr;
  PFN_vkDestroyVideoSessionKHR vkDestroyVideoSessionKHR = nullptr;
  PFN_vkGetVideoSessionMemoryRequirementsKHR
      vkGetVideoSessionMemoryRequirementsKHR = nullptr;
  PFN_vkBindVideoSessionMemoryKHR vkBindVideoSessionMemoryKHR = nullptr;
  PFN_vkCreateVideoSessionParametersKHR vkCreateVideoSessionParametersKHR =
      nullptr;
  PFN_vkDestroyVideoSessionParametersKHR vkDestroyVideoSessionParametersKHR =
      nullptr;
  PFN_vkGetEncodedVideoSessionParametersKHR
      vkGetEncodedVideoSessionParametersKHR = nullptr;
  PFN_vkCmdBeginVideoCodingKHR vkCmdBeginVideoCodingKHR = nullptr;
  PFN_vkCmdEndVideoCodingKHR vkCmdEndVideoCodingKHR = nullptr;
  PFN_vkCmdControlVideoCodingKHR vkCmdControlVideoCodingKHR = nullptr;
  PFN_vkCmdEncodeVideoKHR vkCmdEncodeVideoKHR = nullptr;
};

struct VulkanInstanceDeleter {
  PFN_vkDestroyInstance vkDestroyInstance = nullptr;
  void operator()(VkInstance instance) const;
};
using UniqueVkInstance = std::unique_ptr<VkInstance_T, VulkanInstanceDeleter>;

struct VulkanDeviceDeleter {
  PFN_vkDestroyDevice vkDestroyDevice = nullptr;
  void operator()(VkDevice device) const;
};
using UniqueVkDevice = std::unique_ptr<VkDevice_T, VulkanDeviceDeleter>;

// Returns the Vulkan loader function table. Thread-safe; loads once.
Result<const VulkanFunctions*> TryLoadVulkan();

// Creates an instance that is destroyed when the returned handle goes away.
Result<UniqueVkInstance> CreateVulkanInstance(
    const VulkanFunctions& vulkan, const VkInstanceCreateInfo& create_info);

// Resolves the instance level entry points for `instance`.
Result<VulkanInstanceFunctions> LoadVulkanInstanceFunctions(
    const VulkanFunctions& vulkan, VkInstance instance);

// Creates a device that is destroyed when the returned handle goes away.
Result<UniqueVkDevice> CreateVulkanDevice(
    const VulkanInstanceFunctions& instance_funcs,
    VkPhysicalDevice physical_device, const VkDeviceCreateInfo& create_info);

// Resolves the device level entry points for `device`.
Result<VulkanDeviceFunctions> LoadVulkanDeviceFunctions(
    const VulkanInstanceFunctions& instance_funcs, VkDevice device);

}  // namespace cuttlefish
