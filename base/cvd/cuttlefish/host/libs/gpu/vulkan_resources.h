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

#include <span>

#include "vulkan/vulkan_core.h"

#include "cuttlefish/host/libs/gpu/vulkan_handle.h"
#include "cuttlefish/host/libs/gpu/vulkan_loader.h"
#include "cuttlefish/host/libs/gpu/vulkan_video_context.h"
#include "cuttlefish/result/result.h"

namespace cuttlefish {

// A device allocation and whether its memory type needs explicit flushing.
struct VulkanMemory {
  UniqueVkHandle<VkDeviceMemory> handle;
  bool host_coherent = false;
};

// A buffer in host visible memory, mapped for its whole lifetime. The memory
// is declared first so that it outlives the buffer and the mapping.
struct VulkanMappedBuffer {
  VulkanMemory memory;
  UniqueVkHandle<VkBuffer> buffer;
  VulkanMemoryMapping mapping;
  VkDeviceSize size = 0;
};

// A resettable command pool and the one primary command buffer it holds,
// which is freed with the pool.
struct VulkanCommandBuffer {
  UniqueVkHandle<VkCommandPool> pool;
  VkCommandBuffer buffer = VK_NULL_HANDLE;
};

// Allocates memory of a type that has every bit in `required` and, when the
// device offers one, every bit in `preferred` too.
Result<VulkanMemory> AllocateVulkanMemory(
    const VulkanVideoContext& context, const VkMemoryRequirements& requirements,
    VkMemoryPropertyFlags required, VkMemoryPropertyFlags preferred);

// Allocates memory for `image` as AllocateVulkanMemory does and binds it.
Result<VulkanMemory> AllocateImageMemory(const VulkanVideoContext& context,
                                         VkImage image,
                                         VkMemoryPropertyFlags required,
                                         VkMemoryPropertyFlags preferred);

// Allocates memory for `buffer` as AllocateVulkanMemory does and binds it.
Result<VulkanMemory> AllocateBufferMemory(const VulkanVideoContext& context,
                                          VkBuffer buffer,
                                          VkMemoryPropertyFlags required,
                                          VkMemoryPropertyFlags preferred);

Result<UniqueVkHandle<VkImage>> CreateVulkanImage(
    const VulkanVideoContext& context, const VkImageCreateInfo& info);

Result<UniqueVkHandle<VkImageView>> CreateVulkanImageView(
    const VulkanVideoContext& context, const VkImageViewCreateInfo& info);

Result<UniqueVkHandle<VkBuffer>> CreateVulkanBuffer(
    const VulkanVideoContext& context, const VkBufferCreateInfo& info);

// Creates a buffer in host visible memory, preferably coherent, and maps all
// of it.
Result<VulkanMappedBuffer> CreateMappedBuffer(const VulkanVideoContext& context,
                                              const VkBufferCreateInfo& info);

Result<UniqueVkHandle<VkFence>> CreateVulkanFence(
    const VulkanVideoContext& context);

Result<UniqueVkHandle<VkSemaphore>> CreateVulkanSemaphore(
    const VulkanVideoContext& context);

// Creates a resettable command pool on `queue_family` holding one primary
// command buffer.
Result<VulkanCommandBuffer> CreateVulkanCommandBuffer(
    const VulkanVideoContext& context, uint32_t queue_family);

// A layout transition of the color aspect of an image, and the work on
// either side of it.
struct VulkanImageTransition {
  VkImageLayout old_layout = VK_IMAGE_LAYOUT_UNDEFINED;
  VkImageLayout new_layout = VK_IMAGE_LAYOUT_UNDEFINED;
  VkPipelineStageFlags2 src_stage = VK_PIPELINE_STAGE_2_NONE;
  VkAccessFlags2 src_access = VK_ACCESS_2_NONE;
  VkPipelineStageFlags2 dst_stage = VK_PIPELINE_STAGE_2_NONE;
  VkAccessFlags2 dst_access = VK_ACCESS_2_NONE;
  uint32_t layer_count = 1;
};

// Returns the image memory barrier that performs `transition` on `image`.
VkImageMemoryBarrier2 ImageTransitionBarrier(
    VkImage image, const VulkanImageTransition& transition);

// Records a pipeline barrier holding `barriers`.
void RecordImageBarriers(const VulkanDeviceFunctions& vk,
                         VkCommandBuffer command_buffer,
                         std::span<const VkImageMemoryBarrier2> barriers);

// Records a pipeline barrier holding one image memory barrier.
void RecordImageTransition(const VulkanDeviceFunctions& vk,
                           VkCommandBuffer command_buffer, VkImage image,
                           const VulkanImageTransition& transition);

// Resets `command_buffer` and begins it for a single submission.
Result<void> BeginOneTimeCommands(const VulkanDeviceFunctions& vk,
                                  VkCommandBuffer command_buffer);

// Submits `command_buffer` to the compute queue, signalling `signal_semaphore`
// once it completes.
Result<void> SubmitToComputeQueue(VulkanVideoContext& context,
                                  VkCommandBuffer command_buffer,
                                  VkSemaphore signal_semaphore);

}  // namespace cuttlefish
