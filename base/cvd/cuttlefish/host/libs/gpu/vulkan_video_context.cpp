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

#include "cuttlefish/host/libs/gpu/vulkan_video_context.h"

#include <stdint.h>
#include <string.h>

#include <algorithm>
#include <iterator>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "absl/log/log.h"
#include "vulkan/vulkan_core.h"

#include "cuttlefish/host/libs/gpu/vulkan_av1_capabilities.h"
#include "cuttlefish/host/libs/gpu/vulkan_loader.h"
#include "cuttlefish/result/result.h"

namespace cuttlefish {
namespace {

const char* const kRequiredDeviceExtensions[] = {
    VK_KHR_VIDEO_QUEUE_EXTENSION_NAME,
    VK_KHR_VIDEO_ENCODE_QUEUE_EXTENSION_NAME,
    VK_KHR_VIDEO_ENCODE_AV1_EXTENSION_NAME,
};

// The encode path records vkCmdPipelineBarrier2 barriers and submits with
// vkQueueSubmit2, both core in Vulkan 1.3.
constexpr uint32_t kVulkanApiVersion = VK_API_VERSION_1_3;

constexpr float kQueuePriority = 1.0f;

// A physical device that passed every check, and what the checks found.
struct SelectedDevice {
  VkPhysicalDevice physical_device = VK_NULL_HANDLE;
  std::string name;
  uint32_t encode_queue_family = 0;
  uint32_t compute_queue_family = 0;
  bool sampler_ycbcr_conversion = false;
  VulkanAv1EncodeCapabilities av1_capabilities;
};

std::optional<uint32_t> FindQueueFamily(
    const std::vector<VkQueueFamilyProperties>& families, VkQueueFlags flags) {
  const std::vector<VkQueueFamilyProperties>::const_iterator it =
      std::ranges::find_if(
          families, [flags](const VkQueueFamilyProperties& family) -> bool {
            return (family.queueFlags & flags) != 0;
          });
  if (it == families.end()) {
    return std::nullopt;
  }
  return static_cast<uint32_t>(std::distance(families.begin(), it));
}

Result<std::vector<VkExtensionProperties>> GetDeviceExtensions(
    const VulkanInstanceFunctions& funcs, VkPhysicalDevice device) {
  uint32_t count = 0;
  VkResult res = funcs.vkEnumerateDeviceExtensionProperties(device, nullptr,
                                                            &count, nullptr);
  CF_EXPECT_EQ(res, VK_SUCCESS, "vkEnumerateDeviceExtensionProperties failed");

  std::vector<VkExtensionProperties> extensions(count);
  res = funcs.vkEnumerateDeviceExtensionProperties(device, nullptr, &count,
                                                   extensions.data());
  CF_EXPECT_EQ(res, VK_SUCCESS, "vkEnumerateDeviceExtensionProperties failed");
  extensions.resize(count);
  return extensions;
}

std::vector<VkQueueFamilyProperties> GetQueueFamilies(
    const VulkanInstanceFunctions& funcs, VkPhysicalDevice device) {
  uint32_t count = 0;
  funcs.vkGetPhysicalDeviceQueueFamilyProperties(device, &count, nullptr);

  std::vector<VkQueueFamilyProperties> families(count);
  funcs.vkGetPhysicalDeviceQueueFamilyProperties(device, &count,
                                                 families.data());
  return families;
}

// Fails unless the device offers the features encoding enables. Returns
// whether it also offers sampler YCbCr conversion, which is enabled as
// reported.
Result<bool> CheckDeviceFeatures(const VulkanInstanceFunctions& funcs,
                                 VkPhysicalDevice device) {
  VkPhysicalDeviceVideoEncodeAV1FeaturesKHR av1_features = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VIDEO_ENCODE_AV1_FEATURES_KHR,
      .pNext = nullptr,
      .videoEncodeAV1 = VK_FALSE,
  };
  VkPhysicalDeviceSynchronization2Features synchronization2_features = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SYNCHRONIZATION_2_FEATURES,
      .pNext = &av1_features,
      .synchronization2 = VK_FALSE,
  };
  VkPhysicalDeviceSamplerYcbcrConversionFeatures ycbcr_features = {
      .sType =
          VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SAMPLER_YCBCR_CONVERSION_FEATURES,
      .pNext = &synchronization2_features,
      .samplerYcbcrConversion = VK_FALSE,
  };
  VkPhysicalDeviceFeatures2 features = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
      .pNext = &ycbcr_features,
      .features = {},
  };
  funcs.vkGetPhysicalDeviceFeatures2(device, &features);

  CF_EXPECT_EQ(av1_features.videoEncodeAV1, VK_TRUE,
               "Device does not offer the AV1 encode feature");
  CF_EXPECT_EQ(synchronization2_features.synchronization2, VK_TRUE,
               "Device does not offer synchronization2");
  return ycbcr_features.samplerYcbcrConversion == VK_TRUE;
}

Result<SelectedDevice> CheckPhysicalDevice(const VulkanInstanceFunctions& funcs,
                                           VkPhysicalDevice device) {
  VkPhysicalDeviceProperties properties;
  funcs.vkGetPhysicalDeviceProperties(device, &properties);
  CF_EXPECT_GE(properties.apiVersion, kVulkanApiVersion,
               "Device does not support Vulkan 1.3");

  const std::vector<VkExtensionProperties> extensions =
      CF_EXPECT(GetDeviceExtensions(funcs, device));
  CF_EXPECT(HasRequiredVideoEncodeExtensions(extensions),
            "Video encode extensions missing");

  const std::vector<VkQueueFamilyProperties> families =
      GetQueueFamilies(funcs, device);
  const uint32_t encode_family = CF_EXPECT(FindVideoEncodeQueueFamily(families),
                                           "No video encode queue family");
  const uint32_t compute_family =
      CF_EXPECT(FindComputeQueueFamily(families), "No compute queue family");

  const bool sampler_ycbcr_conversion =
      CF_EXPECT(CheckDeviceFeatures(funcs, device));
  VulkanAv1EncodeCapabilities av1_capabilities =
      CF_EXPECT(QueryVulkanAv1EncodeCapabilities(funcs, device));
  CF_EXPECT(CheckVulkanAv1EncodeCapabilities(av1_capabilities));

  return SelectedDevice{
      .physical_device = device,
      .name = properties.deviceName,
      .encode_queue_family = encode_family,
      .compute_queue_family = compute_family,
      .sampler_ycbcr_conversion = sampler_ycbcr_conversion,
      .av1_capabilities = std::move(av1_capabilities),
  };
}

Result<SelectedDevice> SelectPhysicalDevice(
    const VulkanInstanceFunctions& funcs, VkInstance instance) {
  uint32_t count = 0;
  VkResult res = funcs.vkEnumeratePhysicalDevices(instance, &count, nullptr);
  CF_EXPECT_EQ(res, VK_SUCCESS, "vkEnumeratePhysicalDevices failed");
  CF_EXPECT_GT(count, 0u, "No Vulkan physical device");

  std::vector<VkPhysicalDevice> devices(count);
  res = funcs.vkEnumeratePhysicalDevices(instance, &count, devices.data());
  CF_EXPECT_EQ(res, VK_SUCCESS, "vkEnumeratePhysicalDevices failed");
  devices.resize(count);

  for (const VkPhysicalDevice device : devices) {
    Result<SelectedDevice> selected = CheckPhysicalDevice(funcs, device);
    if (selected.has_value()) {
      return std::move(*selected);
    }
    VLOG(1) << "Vulkan video: skipping physical device: " << selected.error();
  }
  return CF_ERR("No physical device supports AV1 video encode");
}

Result<UniqueVkInstance> CreateInstance(const VulkanFunctions& vulkan) {
  const VkApplicationInfo app_info = {
      .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
      .pNext = nullptr,
      .pApplicationName = "Cuttlefish",
      .applicationVersion = 1,
      .pEngineName = "Cuttlefish",
      .engineVersion = 1,
      .apiVersion = kVulkanApiVersion,
  };
  const VkInstanceCreateInfo instance_info = {
      .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
      .pNext = nullptr,
      .flags = 0,
      .pApplicationInfo = &app_info,
      .enabledLayerCount = 0,
      .ppEnabledLayerNames = nullptr,
      .enabledExtensionCount = 0,
      .ppEnabledExtensionNames = nullptr,
  };
  return CF_EXPECT(CreateVulkanInstance(vulkan, instance_info));
}

std::vector<VkDeviceQueueCreateInfo> QueueCreateInfos(
    const SelectedDevice& selected) {
  std::vector<VkDeviceQueueCreateInfo> queue_infos = {
      VkDeviceQueueCreateInfo{
          .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
          .pNext = nullptr,
          .flags = 0,
          .queueFamilyIndex = selected.encode_queue_family,
          .queueCount = 1,
          .pQueuePriorities = &kQueuePriority,
      },
  };
  // A family may only be named once.
  if (selected.compute_queue_family != selected.encode_queue_family) {
    queue_infos.push_back(VkDeviceQueueCreateInfo{
        .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .queueFamilyIndex = selected.compute_queue_family,
        .queueCount = 1,
        .pQueuePriorities = &kQueuePriority,
    });
  }
  return queue_infos;
}

Result<UniqueVkDevice> CreateDevice(const VulkanInstanceFunctions& funcs,
                                    const SelectedDevice& selected) {
  VkPhysicalDeviceVideoEncodeAV1FeaturesKHR av1_features = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VIDEO_ENCODE_AV1_FEATURES_KHR,
      .pNext = nullptr,
      .videoEncodeAV1 = VK_TRUE,
  };
  VkPhysicalDeviceSynchronization2Features synchronization2_features = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SYNCHRONIZATION_2_FEATURES,
      .pNext = &av1_features,
      .synchronization2 = VK_TRUE,
  };
  VkPhysicalDeviceSamplerYcbcrConversionFeatures ycbcr_features = {
      .sType =
          VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SAMPLER_YCBCR_CONVERSION_FEATURES,
      .pNext = &synchronization2_features,
      .samplerYcbcrConversion =
          selected.sampler_ycbcr_conversion ? VK_TRUE : VK_FALSE,
  };
  const VkPhysicalDeviceFeatures2 features = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
      .pNext = &ycbcr_features,
      .features = {},
  };

  const std::vector<VkDeviceQueueCreateInfo> queue_infos =
      QueueCreateInfos(selected);
  const VkDeviceCreateInfo device_info = {
      .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
      .pNext = &features,
      .flags = 0,
      .queueCreateInfoCount = static_cast<uint32_t>(queue_infos.size()),
      .pQueueCreateInfos = queue_infos.data(),
      .enabledLayerCount = 0,
      .ppEnabledLayerNames = nullptr,
      .enabledExtensionCount =
          static_cast<uint32_t>(std::size(kRequiredDeviceExtensions)),
      .ppEnabledExtensionNames = kRequiredDeviceExtensions,
      .pEnabledFeatures = nullptr,
  };
  return CF_EXPECT(
      CreateVulkanDevice(funcs, selected.physical_device, device_info));
}

Result<VulkanQueue> GetQueue(const VulkanDeviceFunctions& funcs,
                             VkDevice device, uint32_t family) {
  VulkanQueue queue = {.family = family};
  funcs.vkGetDeviceQueue(device, family, 0, &queue.queue);
  CF_EXPECT_NE(queue.queue, nullptr, "No queue in family " << family);
  return queue;
}

bool ProbeVulkanAv1Encode() {
  Result<std::shared_ptr<VulkanVideoContext>> context =
      VulkanVideoContext::Get();
  if (!context.has_value()) {
    VLOG(1) << "Vulkan AV1 encode not available: " << context.error();
    return false;
  }
  const VkExtent2D max_extent = (*context)->av1_capabilities().max_coded_extent;
  LOG(INFO) << "Vulkan AV1 encode available, up to " << max_extent.width << "x"
            << max_extent.height;
  return true;
}

}  // namespace

bool HasRequiredVideoEncodeExtensions(
    const std::vector<VkExtensionProperties>& extensions) {
  return std::ranges::all_of(
      kRequiredDeviceExtensions, [&extensions](const char* required) -> bool {
        return std::ranges::any_of(
            extensions,
            [required](const VkExtensionProperties& extension) -> bool {
              return strcmp(extension.extensionName, required) == 0;
            });
      });
}

std::optional<uint32_t> FindVideoEncodeQueueFamily(
    const std::vector<VkQueueFamilyProperties>& families) {
  return FindQueueFamily(families, VK_QUEUE_VIDEO_ENCODE_BIT_KHR);
}

std::optional<uint32_t> FindComputeQueueFamily(
    const std::vector<VkQueueFamilyProperties>& families) {
  return FindQueueFamily(families, VK_QUEUE_COMPUTE_BIT);
}

bool IsVulkanAv1EncodeSupported() {
  static const bool supported = ProbeVulkanAv1Encode();
  return supported;
}

Result<std::shared_ptr<VulkanVideoContext>> VulkanVideoContext::Get() {
  static std::mutex mutex;
  static std::weak_ptr<VulkanVideoContext> instance;

  std::lock_guard<std::mutex> lock(mutex);

  std::shared_ptr<VulkanVideoContext> shared = instance.lock();
  if (shared) {
    return shared;
  }
  shared = CF_EXPECT(Create());
  instance = shared;
  return shared;
}

Result<std::shared_ptr<VulkanVideoContext>> VulkanVideoContext::Create() {
  const VulkanFunctions* vulkan = CF_EXPECT(TryLoadVulkan());
  UniqueVkInstance instance = CF_EXPECT(CreateInstance(*vulkan));
  const VulkanInstanceFunctions instance_funcs =
      CF_EXPECT(LoadVulkanInstanceFunctions(*vulkan, instance.get()));
  SelectedDevice selected =
      CF_EXPECT(SelectPhysicalDevice(instance_funcs, instance.get()));

  UniqueVkDevice device = CF_EXPECT(CreateDevice(instance_funcs, selected));
  const VulkanDeviceFunctions device_funcs =
      CF_EXPECT(LoadVulkanDeviceFunctions(instance_funcs, device.get()));
  const VulkanQueue encode_queue = CF_EXPECT(
      GetQueue(device_funcs, device.get(), selected.encode_queue_family));
  const VulkanQueue compute_queue = CF_EXPECT(
      GetQueue(device_funcs, device.get(), selected.compute_queue_family));

  VLOG(1) << "Vulkan video encode device: " << selected.name
          << " (encode queue family " << encode_queue.family
          << ", compute queue family " << compute_queue.family << ")";
  return std::shared_ptr<VulkanVideoContext>(new VulkanVideoContext(
      std::move(instance), instance_funcs, selected.physical_device,
      std::move(selected.av1_capabilities), std::move(device), device_funcs,
      encode_queue, compute_queue));
}

VulkanVideoContext::VulkanVideoContext(
    UniqueVkInstance instance, const VulkanInstanceFunctions& instance_funcs,
    VkPhysicalDevice physical_device,
    VulkanAv1EncodeCapabilities av1_capabilities, UniqueVkDevice device,
    const VulkanDeviceFunctions& device_funcs, VulkanQueue encode_queue,
    VulkanQueue compute_queue)
    : instance_(std::move(instance)),
      instance_funcs_(instance_funcs),
      physical_device_(physical_device),
      av1_capabilities_(std::move(av1_capabilities)),
      device_(std::move(device)),
      device_funcs_(device_funcs),
      encode_queue_(encode_queue),
      compute_queue_(compute_queue) {
  instance_funcs_.vkGetPhysicalDeviceMemoryProperties(physical_device_,
                                                      &memory_properties_);
}

std::optional<uint32_t> VulkanVideoContext::FindMemoryType(
    uint32_t type_bits, VkMemoryPropertyFlags properties) const {
  for (uint32_t i = 0; i < memory_properties_.memoryTypeCount; i++) {
    if ((type_bits & (1u << i)) == 0) {
      continue;
    }
    if ((memory_properties_.memoryTypes[i].propertyFlags & properties) ==
        properties) {
      return i;
    }
  }
  return std::nullopt;
}

}  // namespace cuttlefish
