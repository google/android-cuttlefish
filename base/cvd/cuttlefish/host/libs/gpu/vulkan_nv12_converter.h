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

#include "vulkan/vulkan_core.h"

#include "cuttlefish/host/libs/gpu/rgba_to_nv12.h"
#include "cuttlefish/host/libs/gpu/vulkan_handle.h"
#include "cuttlefish/host/libs/gpu/vulkan_resources.h"
#include "cuttlefish/host/libs/gpu/vulkan_video_context.h"
#include "cuttlefish/result/result.h"

namespace cuttlefish {

// Returns the workgroup count for a coded size. One invocation covers a 2x2
// block, so a partial workgroup at the right or bottom edge is rounded up and
// the extra invocations return early.
VkExtent2D Nv12DispatchGroupCount(uint32_t coded_width, uint32_t coded_height);

// Fails with the reason where this device cannot run the conversion shader,
// which is when it refuses a storage image in one of the two plane formats.
Result<void> CheckNv12ConversionSupport(const VulkanVideoContext& context);

// Converts packed RGBA into the encode input image with a compute shader. Not
// thread-safe. The owner must make sure no conversion is pending before
// destroying it.
class VulkanNv12Converter {
 public:
  // `image` is the encode input image, which must carry the transfer
  // destination usage and be reachable from the compute queue family.
  // `source` is a buffer with the storage usage holding the packed RGBA
  // frame.
  static Result<std::unique_ptr<VulkanNv12Converter>> Create(
      std::shared_ptr<VulkanVideoContext> context, VkImage image,
      VkExtent2D coded_extent, VkBuffer source, VkDeviceSize source_size);

  VulkanNv12Converter(const VulkanNv12Converter&) = delete;
  VulkanNv12Converter& operator=(const VulkanNv12Converter&) = delete;

  // Submits the conversion without waiting and leaves the encode input image
  // in the transfer destination layout. done_semaphore() signals completion.
  Result<void> Convert(const Nv12ConversionParams& params);

  // Returns the semaphore signalled once the converted frame is in the encode
  // input image. The encode submission waits on it.
  VkSemaphore done_semaphore() const { return done_semaphore_.get(); }

 private:
  VulkanNv12Converter(std::shared_ptr<VulkanVideoContext> context,
                      VkImage image, VkExtent2D coded_extent);

  Result<void> CreatePlaneImages();
  Result<void> CreatePipeline();
  Result<void> CreateDescriptors(VkBuffer source, VkDeviceSize source_size);
  Result<void> CreateCommandResources();

  void RecordConversion(const Nv12ConversionParams& params);
  void RecordPlaneCopies();

  std::shared_ptr<VulkanVideoContext> context_;
  VkImage image_ = VK_NULL_HANDLE;
  VkExtent2D coded_extent_ = {};

  VulkanMemory luma_memory_;
  VulkanMemory chroma_memory_;
  UniqueVkHandle<VkImage> luma_image_;
  UniqueVkHandle<VkImage> chroma_image_;
  UniqueVkHandle<VkImageView> luma_view_;
  UniqueVkHandle<VkImageView> chroma_view_;

  UniqueVkHandle<VkShaderModule> shader_;
  UniqueVkHandle<VkDescriptorSetLayout> descriptor_layout_;
  UniqueVkHandle<VkPipelineLayout> pipeline_layout_;
  UniqueVkHandle<VkPipeline> pipeline_;
  UniqueVkHandle<VkDescriptorPool> descriptor_pool_;
  VkDescriptorSet descriptor_set_ = VK_NULL_HANDLE;
  VulkanCommandBuffer command_;
  UniqueVkHandle<VkSemaphore> done_semaphore_;
};

}  // namespace cuttlefish
