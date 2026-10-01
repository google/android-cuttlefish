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

#include "cuttlefish/host/libs/gpu/vulkan_loader.h"

#include <dlfcn.h>

#include "vulkan/vulkan_core.h"

#include "cuttlefish/result/result.h"

namespace cuttlefish {
namespace {

// Owns the dlopen handle. Never calls dlclose (same as CudaLibrary): the
// loader and the ICDs behind it keep global state that does not survive an
// unload.
struct VulkanLibrary {
  void* handle = nullptr;
  VulkanFunctions funcs{};
};

template <typename GetProcAddr, typename Handle, typename Fn>
Result<void> LoadVulkanSymbol(GetProcAddr get_proc_addr, Handle handle,
                              const char* name, Fn* out) {
  *out = reinterpret_cast<Fn>(get_proc_addr(handle, name));
  CF_EXPECT_NE(*out, nullptr, "Vulkan entry point not available: " << name);
  return {};
}

Result<VulkanLibrary> LoadVulkan() {
  VulkanLibrary lib;
  lib.handle = dlopen("libvulkan.so.1", RTLD_LAZY);
  CF_EXPECT_NE(lib.handle, nullptr,
               "libvulkan.so.1 not available: " << dlerror());

  VulkanFunctions& f = lib.funcs;
  f.vkGetInstanceProcAddr = reinterpret_cast<PFN_vkGetInstanceProcAddr>(
      dlsym(lib.handle, "vkGetInstanceProcAddr"));
  CF_EXPECT_NE(f.vkGetInstanceProcAddr, nullptr,
               "Cannot load vkGetInstanceProcAddr");

  // A null instance resolves the entry points that exist before one.
  CF_EXPECT(LoadVulkanSymbol(f.vkGetInstanceProcAddr, nullptr,
                             "vkCreateInstance", &f.vkCreateInstance));

  return lib;
}

}  // namespace

void VulkanInstanceDeleter::operator()(VkInstance instance) const {
  vkDestroyInstance(instance, nullptr);
}

void VulkanDeviceDeleter::operator()(VkDevice device) const {
  vkDestroyDevice(device, nullptr);
}

Result<const VulkanFunctions*> TryLoadVulkan() {
  static Result<VulkanLibrary> cached = LoadVulkan();
  if (!cached.has_value()) {
    return CF_ERR(cached.error());
  }
  return &(cached.value().funcs);
}

Result<UniqueVkInstance> CreateVulkanInstance(
    const VulkanFunctions& vulkan, const VkInstanceCreateInfo& create_info) {
  VkInstance instance = VK_NULL_HANDLE;
  const VkResult res =
      vulkan.vkCreateInstance(&create_info, nullptr, &instance);
  CF_EXPECT_EQ(res, VK_SUCCESS, "vkCreateInstance failed");
  VulkanInstanceDeleter deleter;
  CF_EXPECT(LoadVulkanSymbol(vulkan.vkGetInstanceProcAddr, instance,
                             "vkDestroyInstance", &deleter.vkDestroyInstance));
  return UniqueVkInstance(instance, deleter);
}

Result<VulkanInstanceFunctions> LoadVulkanInstanceFunctions(
    const VulkanFunctions& vulkan, VkInstance instance) {
  const PFN_vkGetInstanceProcAddr get = vulkan.vkGetInstanceProcAddr;
  VulkanInstanceFunctions f;

  CF_EXPECT(LoadVulkanSymbol(get, instance, "vkEnumeratePhysicalDevices",
                             &f.vkEnumeratePhysicalDevices));
  CF_EXPECT(LoadVulkanSymbol(get, instance, "vkGetPhysicalDeviceProperties",
                             &f.vkGetPhysicalDeviceProperties));
  CF_EXPECT(LoadVulkanSymbol(get, instance, "vkGetPhysicalDeviceFeatures2",
                             &f.vkGetPhysicalDeviceFeatures2));
  CF_EXPECT(LoadVulkanSymbol(get, instance,
                             "vkGetPhysicalDeviceMemoryProperties",
                             &f.vkGetPhysicalDeviceMemoryProperties));
  CF_EXPECT(LoadVulkanSymbol(get, instance,
                             "vkGetPhysicalDeviceQueueFamilyProperties",
                             &f.vkGetPhysicalDeviceQueueFamilyProperties));
  CF_EXPECT(LoadVulkanSymbol(get, instance,
                             "vkEnumerateDeviceExtensionProperties",
                             &f.vkEnumerateDeviceExtensionProperties));
  CF_EXPECT(LoadVulkanSymbol(get, instance,
                             "vkGetPhysicalDeviceVideoCapabilitiesKHR",
                             &f.vkGetPhysicalDeviceVideoCapabilitiesKHR));
  CF_EXPECT(LoadVulkanSymbol(get, instance,
                             "vkGetPhysicalDeviceVideoFormatPropertiesKHR",
                             &f.vkGetPhysicalDeviceVideoFormatPropertiesKHR));
  CF_EXPECT(
      LoadVulkanSymbol(get, instance, "vkCreateDevice", &f.vkCreateDevice));
  CF_EXPECT(LoadVulkanSymbol(get, instance, "vkGetDeviceProcAddr",
                             &f.vkGetDeviceProcAddr));

  return f;
}

Result<UniqueVkDevice> CreateVulkanDevice(
    const VulkanInstanceFunctions& instance_funcs,
    VkPhysicalDevice physical_device, const VkDeviceCreateInfo& create_info) {
  VkDevice device = VK_NULL_HANDLE;
  const VkResult res = instance_funcs.vkCreateDevice(
      physical_device, &create_info, nullptr, &device);
  CF_EXPECT_EQ(res, VK_SUCCESS, "vkCreateDevice failed");
  VulkanDeviceDeleter deleter;
  CF_EXPECT(LoadVulkanSymbol(instance_funcs.vkGetDeviceProcAddr, device,
                             "vkDestroyDevice", &deleter.vkDestroyDevice));
  return UniqueVkDevice(device, deleter);
}

Result<VulkanDeviceFunctions> LoadVulkanDeviceFunctions(
    const VulkanInstanceFunctions& instance_funcs, VkDevice device) {
  const PFN_vkGetDeviceProcAddr get = instance_funcs.vkGetDeviceProcAddr;
  VulkanDeviceFunctions f;

  CF_EXPECT(
      LoadVulkanSymbol(get, device, "vkDeviceWaitIdle", &f.vkDeviceWaitIdle));
  CF_EXPECT(
      LoadVulkanSymbol(get, device, "vkGetDeviceQueue", &f.vkGetDeviceQueue));
  CF_EXPECT(LoadVulkanSymbol(get, device, "vkQueueSubmit2", &f.vkQueueSubmit2));

  CF_EXPECT(
      LoadVulkanSymbol(get, device, "vkAllocateMemory", &f.vkAllocateMemory));
  CF_EXPECT(LoadVulkanSymbol(get, device, "vkFreeMemory", &f.vkFreeMemory));
  CF_EXPECT(LoadVulkanSymbol(get, device, "vkMapMemory", &f.vkMapMemory));
  CF_EXPECT(LoadVulkanSymbol(get, device, "vkUnmapMemory", &f.vkUnmapMemory));
  CF_EXPECT(LoadVulkanSymbol(get, device, "vkFlushMappedMemoryRanges",
                             &f.vkFlushMappedMemoryRanges));
  CF_EXPECT(LoadVulkanSymbol(get, device, "vkInvalidateMappedMemoryRanges",
                             &f.vkInvalidateMappedMemoryRanges));

  CF_EXPECT(LoadVulkanSymbol(get, device, "vkCreateBuffer", &f.vkCreateBuffer));
  CF_EXPECT(
      LoadVulkanSymbol(get, device, "vkDestroyBuffer", &f.vkDestroyBuffer));
  CF_EXPECT(LoadVulkanSymbol(get, device, "vkGetBufferMemoryRequirements",
                             &f.vkGetBufferMemoryRequirements));
  CF_EXPECT(LoadVulkanSymbol(get, device, "vkBindBufferMemory",
                             &f.vkBindBufferMemory));
  CF_EXPECT(LoadVulkanSymbol(get, device, "vkCreateImage", &f.vkCreateImage));
  CF_EXPECT(LoadVulkanSymbol(get, device, "vkDestroyImage", &f.vkDestroyImage));
  CF_EXPECT(LoadVulkanSymbol(get, device, "vkGetImageMemoryRequirements",
                             &f.vkGetImageMemoryRequirements));
  CF_EXPECT(
      LoadVulkanSymbol(get, device, "vkBindImageMemory", &f.vkBindImageMemory));
  CF_EXPECT(
      LoadVulkanSymbol(get, device, "vkCreateImageView", &f.vkCreateImageView));
  CF_EXPECT(LoadVulkanSymbol(get, device, "vkDestroyImageView",
                             &f.vkDestroyImageView));

  CF_EXPECT(LoadVulkanSymbol(get, device, "vkCreateCommandPool",
                             &f.vkCreateCommandPool));
  CF_EXPECT(LoadVulkanSymbol(get, device, "vkDestroyCommandPool",
                             &f.vkDestroyCommandPool));
  CF_EXPECT(LoadVulkanSymbol(get, device, "vkAllocateCommandBuffers",
                             &f.vkAllocateCommandBuffers));
  CF_EXPECT(LoadVulkanSymbol(get, device, "vkBeginCommandBuffer",
                             &f.vkBeginCommandBuffer));
  CF_EXPECT(LoadVulkanSymbol(get, device, "vkEndCommandBuffer",
                             &f.vkEndCommandBuffer));
  CF_EXPECT(LoadVulkanSymbol(get, device, "vkResetCommandBuffer",
                             &f.vkResetCommandBuffer));
  CF_EXPECT(LoadVulkanSymbol(get, device, "vkCreateFence", &f.vkCreateFence));
  CF_EXPECT(LoadVulkanSymbol(get, device, "vkDestroyFence", &f.vkDestroyFence));
  CF_EXPECT(
      LoadVulkanSymbol(get, device, "vkWaitForFences", &f.vkWaitForFences));
  CF_EXPECT(LoadVulkanSymbol(get, device, "vkResetFences", &f.vkResetFences));
  CF_EXPECT(
      LoadVulkanSymbol(get, device, "vkCreateSemaphore", &f.vkCreateSemaphore));
  CF_EXPECT(LoadVulkanSymbol(get, device, "vkDestroySemaphore",
                             &f.vkDestroySemaphore));

  CF_EXPECT(
      LoadVulkanSymbol(get, device, "vkCreateQueryPool", &f.vkCreateQueryPool));
  CF_EXPECT(LoadVulkanSymbol(get, device, "vkDestroyQueryPool",
                             &f.vkDestroyQueryPool));
  CF_EXPECT(LoadVulkanSymbol(get, device, "vkGetQueryPoolResults",
                             &f.vkGetQueryPoolResults));
  CF_EXPECT(LoadVulkanSymbol(get, device, "vkCmdResetQueryPool",
                             &f.vkCmdResetQueryPool));
  CF_EXPECT(
      LoadVulkanSymbol(get, device, "vkCmdBeginQuery", &f.vkCmdBeginQuery));
  CF_EXPECT(LoadVulkanSymbol(get, device, "vkCmdEndQuery", &f.vkCmdEndQuery));
  CF_EXPECT(LoadVulkanSymbol(get, device, "vkCmdCopyBufferToImage",
                             &f.vkCmdCopyBufferToImage));
  CF_EXPECT(LoadVulkanSymbol(get, device, "vkCmdPipelineBarrier2",
                             &f.vkCmdPipelineBarrier2));

  CF_EXPECT(LoadVulkanSymbol(get, device, "vkCreateVideoSessionKHR",
                             &f.vkCreateVideoSessionKHR));
  CF_EXPECT(LoadVulkanSymbol(get, device, "vkDestroyVideoSessionKHR",
                             &f.vkDestroyVideoSessionKHR));
  CF_EXPECT(LoadVulkanSymbol(get, device,
                             "vkGetVideoSessionMemoryRequirementsKHR",
                             &f.vkGetVideoSessionMemoryRequirementsKHR));
  CF_EXPECT(LoadVulkanSymbol(get, device, "vkBindVideoSessionMemoryKHR",
                             &f.vkBindVideoSessionMemoryKHR));
  CF_EXPECT(LoadVulkanSymbol(get, device, "vkCreateVideoSessionParametersKHR",
                             &f.vkCreateVideoSessionParametersKHR));
  CF_EXPECT(LoadVulkanSymbol(get, device, "vkDestroyVideoSessionParametersKHR",
                             &f.vkDestroyVideoSessionParametersKHR));
  CF_EXPECT(LoadVulkanSymbol(get, device,
                             "vkGetEncodedVideoSessionParametersKHR",
                             &f.vkGetEncodedVideoSessionParametersKHR));
  CF_EXPECT(LoadVulkanSymbol(get, device, "vkCmdBeginVideoCodingKHR",
                             &f.vkCmdBeginVideoCodingKHR));
  CF_EXPECT(LoadVulkanSymbol(get, device, "vkCmdEndVideoCodingKHR",
                             &f.vkCmdEndVideoCodingKHR));
  CF_EXPECT(LoadVulkanSymbol(get, device, "vkCmdControlVideoCodingKHR",
                             &f.vkCmdControlVideoCodingKHR));
  CF_EXPECT(LoadVulkanSymbol(get, device, "vkCmdEncodeVideoKHR",
                             &f.vkCmdEncodeVideoKHR));
  return f;
}

}  // namespace cuttlefish
