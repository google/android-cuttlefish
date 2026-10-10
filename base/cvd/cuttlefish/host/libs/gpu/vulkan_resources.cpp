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

#include "cuttlefish/host/libs/gpu/vulkan_resources.h"

#include <stdint.h>

#include <mutex>
#include <optional>
#include <span>

#include "vulkan/vulkan_core.h"

#include "cuttlefish/host/libs/gpu/vulkan_handle.h"
#include "cuttlefish/host/libs/gpu/vulkan_loader.h"
#include "cuttlefish/host/libs/gpu/vulkan_video_context.h"
#include "cuttlefish/result/result.h"

namespace cuttlefish {

Result<VulkanMemory> AllocateVulkanMemory(
    const VulkanVideoContext& context, const VkMemoryRequirements& requirements,
    VkMemoryPropertyFlags required, VkMemoryPropertyFlags preferred) {
  const VulkanDeviceFunctions& vk = context.device_functions();

  std::optional<uint32_t> type =
      context.FindMemoryType(requirements.memoryTypeBits, required | preferred);
  if (!type.has_value()) {
    type = context.FindMemoryType(requirements.memoryTypeBits, required);
  }
  CF_EXPECTF(type.has_value(), "No memory type with properties {:#x}",
             required);

  const VkMemoryAllocateInfo allocate_info = {
      .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
      .pNext = nullptr,
      .allocationSize = requirements.size,
      .memoryTypeIndex = *type,
  };
  VkDeviceMemory memory = VK_NULL_HANDLE;
  const VkResult res =
      vk.vkAllocateMemory(context.device(), &allocate_info, nullptr, &memory);
  CF_EXPECT_EQ(res, VK_SUCCESS, "vkAllocateMemory failed");
  return VulkanMemory{
      .handle = UniqueVkHandle<VkDeviceMemory>(context.device(), memory,
                                               vk.vkFreeMemory),
      .host_coherent =
          (context.memory_properties().memoryTypes[*type].propertyFlags &
           VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) != 0,
  };
}

Result<VulkanMemory> AllocateImageMemory(const VulkanVideoContext& context,
                                         VkImage image,
                                         VkMemoryPropertyFlags required,
                                         VkMemoryPropertyFlags preferred) {
  const VulkanDeviceFunctions& vk = context.device_functions();

  VkMemoryRequirements requirements;
  vk.vkGetImageMemoryRequirements(context.device(), image, &requirements);
  VulkanMemory memory = CF_EXPECT(
      AllocateVulkanMemory(context, requirements, required, preferred));
  const VkResult res =
      vk.vkBindImageMemory(context.device(), image, memory.handle.get(), 0);
  CF_EXPECT_EQ(res, VK_SUCCESS, "vkBindImageMemory failed");
  return memory;
}

Result<VulkanMemory> AllocateBufferMemory(const VulkanVideoContext& context,
                                          VkBuffer buffer,
                                          VkMemoryPropertyFlags required,
                                          VkMemoryPropertyFlags preferred) {
  const VulkanDeviceFunctions& vk = context.device_functions();

  VkMemoryRequirements requirements;
  vk.vkGetBufferMemoryRequirements(context.device(), buffer, &requirements);
  VulkanMemory memory = CF_EXPECT(
      AllocateVulkanMemory(context, requirements, required, preferred));
  const VkResult res =
      vk.vkBindBufferMemory(context.device(), buffer, memory.handle.get(), 0);
  CF_EXPECT_EQ(res, VK_SUCCESS, "vkBindBufferMemory failed");
  return memory;
}

Result<UniqueVkHandle<VkImage>> CreateVulkanImage(
    const VulkanVideoContext& context, const VkImageCreateInfo& info) {
  const VulkanDeviceFunctions& vk = context.device_functions();

  VkImage image = VK_NULL_HANDLE;
  const VkResult res =
      vk.vkCreateImage(context.device(), &info, nullptr, &image);
  CF_EXPECT_EQ(res, VK_SUCCESS, "vkCreateImage failed");
  return UniqueVkHandle<VkImage>(context.device(), image, vk.vkDestroyImage);
}

Result<UniqueVkHandle<VkImageView>> CreateVulkanImageView(
    const VulkanVideoContext& context, const VkImageViewCreateInfo& info) {
  const VulkanDeviceFunctions& vk = context.device_functions();

  VkImageView view = VK_NULL_HANDLE;
  const VkResult res =
      vk.vkCreateImageView(context.device(), &info, nullptr, &view);
  CF_EXPECT_EQ(res, VK_SUCCESS, "vkCreateImageView failed");
  return UniqueVkHandle<VkImageView>(context.device(), view,
                                     vk.vkDestroyImageView);
}

Result<UniqueVkHandle<VkBuffer>> CreateVulkanBuffer(
    const VulkanVideoContext& context, const VkBufferCreateInfo& info) {
  const VulkanDeviceFunctions& vk = context.device_functions();

  VkBuffer buffer = VK_NULL_HANDLE;
  const VkResult res =
      vk.vkCreateBuffer(context.device(), &info, nullptr, &buffer);
  CF_EXPECT_EQ(res, VK_SUCCESS, "vkCreateBuffer failed");
  return UniqueVkHandle<VkBuffer>(context.device(), buffer, vk.vkDestroyBuffer);
}

Result<VulkanMappedBuffer> CreateMappedBuffer(const VulkanVideoContext& context,
                                              const VkBufferCreateInfo& info) {
  const VulkanDeviceFunctions& vk = context.device_functions();

  VulkanMappedBuffer mapped = {.size = info.size};
  mapped.buffer = CF_EXPECT(CreateVulkanBuffer(context, info));
  mapped.memory = CF_EXPECT(AllocateBufferMemory(
      context, mapped.buffer.get(), VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
      VK_MEMORY_PROPERTY_HOST_COHERENT_BIT));

  void* data = nullptr;
  const VkResult res = vk.vkMapMemory(
      context.device(), mapped.memory.handle.get(), 0, VK_WHOLE_SIZE, 0, &data);
  CF_EXPECT_EQ(res, VK_SUCCESS, "vkMapMemory failed");
  CF_EXPECT_NE(data, nullptr, "vkMapMemory returned no pointer");
  mapped.mapping = VulkanMemoryMapping(
      context.device(), mapped.memory.handle.get(), data, vk.vkUnmapMemory);
  return mapped;
}

Result<UniqueVkHandle<VkFence>> CreateVulkanFence(
    const VulkanVideoContext& context) {
  const VulkanDeviceFunctions& vk = context.device_functions();

  const VkFenceCreateInfo fence_info = {
      .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
      .pNext = nullptr,
      .flags = 0,
  };
  VkFence fence = VK_NULL_HANDLE;
  const VkResult res =
      vk.vkCreateFence(context.device(), &fence_info, nullptr, &fence);
  CF_EXPECT_EQ(res, VK_SUCCESS, "vkCreateFence failed");
  return UniqueVkHandle<VkFence>(context.device(), fence, vk.vkDestroyFence);
}

Result<UniqueVkHandle<VkSemaphore>> CreateVulkanSemaphore(
    const VulkanVideoContext& context) {
  const VulkanDeviceFunctions& vk = context.device_functions();

  const VkSemaphoreCreateInfo semaphore_info = {
      .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
      .pNext = nullptr,
      .flags = 0,
  };
  VkSemaphore semaphore = VK_NULL_HANDLE;
  const VkResult res = vk.vkCreateSemaphore(context.device(), &semaphore_info,
                                            nullptr, &semaphore);
  CF_EXPECT_EQ(res, VK_SUCCESS, "vkCreateSemaphore failed");
  return UniqueVkHandle<VkSemaphore>(context.device(), semaphore,
                                     vk.vkDestroySemaphore);
}

Result<VulkanCommandBuffer> CreateVulkanCommandBuffer(
    const VulkanVideoContext& context, uint32_t queue_family) {
  const VulkanDeviceFunctions& vk = context.device_functions();

  const VkCommandPoolCreateInfo pool_info = {
      .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
      .pNext = nullptr,
      .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
      .queueFamilyIndex = queue_family,
  };
  VkCommandPool pool = VK_NULL_HANDLE;
  VkResult res =
      vk.vkCreateCommandPool(context.device(), &pool_info, nullptr, &pool);
  CF_EXPECT_EQ(res, VK_SUCCESS, "vkCreateCommandPool failed");
  VulkanCommandBuffer command_buffer = {
      .pool = UniqueVkHandle<VkCommandPool>(context.device(), pool,
                                            vk.vkDestroyCommandPool),
  };

  const VkCommandBufferAllocateInfo allocate_info = {
      .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
      .pNext = nullptr,
      .commandPool = pool,
      .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
      .commandBufferCount = 1,
  };
  res = vk.vkAllocateCommandBuffers(context.device(), &allocate_info,
                                    &command_buffer.buffer);
  CF_EXPECT_EQ(res, VK_SUCCESS, "vkAllocateCommandBuffers failed");
  return command_buffer;
}

VkImageMemoryBarrier2 ImageTransitionBarrier(
    VkImage image, const VulkanImageTransition& transition) {
  return VkImageMemoryBarrier2{
      .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
      .pNext = nullptr,
      .srcStageMask = transition.src_stage,
      .srcAccessMask = transition.src_access,
      .dstStageMask = transition.dst_stage,
      .dstAccessMask = transition.dst_access,
      .oldLayout = transition.old_layout,
      .newLayout = transition.new_layout,
      .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
      .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
      .image = image,
      .subresourceRange =
          {
              .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
              .baseMipLevel = 0,
              .levelCount = 1,
              .baseArrayLayer = 0,
              .layerCount = transition.layer_count,
          },
  };
}

void RecordImageBarriers(const VulkanDeviceFunctions& vk,
                         VkCommandBuffer command_buffer,
                         std::span<const VkImageMemoryBarrier2> barriers) {
  const VkDependencyInfo dependency = {
      .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
      .pNext = nullptr,
      .dependencyFlags = 0,
      .memoryBarrierCount = 0,
      .pMemoryBarriers = nullptr,
      .bufferMemoryBarrierCount = 0,
      .pBufferMemoryBarriers = nullptr,
      .imageMemoryBarrierCount = static_cast<uint32_t>(barriers.size()),
      .pImageMemoryBarriers = barriers.data(),
  };
  vk.vkCmdPipelineBarrier2(command_buffer, &dependency);
}

void RecordImageTransition(const VulkanDeviceFunctions& vk,
                           VkCommandBuffer command_buffer, VkImage image,
                           const VulkanImageTransition& transition) {
  const VkImageMemoryBarrier2 barrier =
      ImageTransitionBarrier(image, transition);
  RecordImageBarriers(vk, command_buffer, {&barrier, 1});
}

Result<void> BeginOneTimeCommands(const VulkanDeviceFunctions& vk,
                                  VkCommandBuffer command_buffer) {
  VkResult res = vk.vkResetCommandBuffer(command_buffer, 0);
  CF_EXPECT_EQ(res, VK_SUCCESS, "vkResetCommandBuffer failed");

  const VkCommandBufferBeginInfo begin_info = {
      .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
      .pNext = nullptr,
      .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
      .pInheritanceInfo = nullptr,
  };
  res = vk.vkBeginCommandBuffer(command_buffer, &begin_info);
  CF_EXPECT_EQ(res, VK_SUCCESS, "vkBeginCommandBuffer failed");
  return {};
}

Result<void> SubmitToComputeQueue(VulkanVideoContext& context,
                                  VkCommandBuffer command_buffer,
                                  VkSemaphore signal_semaphore) {
  const VkCommandBufferSubmitInfo command_buffer_info = {
      .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO,
      .pNext = nullptr,
      .commandBuffer = command_buffer,
      .deviceMask = 0,
  };
  const VkSemaphoreSubmitInfo signal_info = {
      .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
      .pNext = nullptr,
      .semaphore = signal_semaphore,
      .value = 0,
      .stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
      .deviceIndex = 0,
  };
  const VkSubmitInfo2 submit_info = {
      .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
      .pNext = nullptr,
      .flags = 0,
      .waitSemaphoreInfoCount = 0,
      .pWaitSemaphoreInfos = nullptr,
      .commandBufferInfoCount = 1,
      .pCommandBufferInfos = &command_buffer_info,
      .signalSemaphoreInfoCount = 1,
      .pSignalSemaphoreInfos = &signal_info,
  };

  // Every encoder in the process shares the one compute queue.
  const std::lock_guard<std::mutex> lock(context.compute_queue_mutex());
  const VkResult res = context.device_functions().vkQueueSubmit2(
      context.compute_queue(), 1, &submit_info, VK_NULL_HANDLE);
  CF_EXPECT_EQ(res, VK_SUCCESS, "vkQueueSubmit2 failed");
  return {};
}

}  // namespace cuttlefish
