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

#include "cuttlefish/host/libs/gpu/vulkan_nv12_converter.h"

#include <stddef.h>
#include <stdint.h>

#include <iterator>
#include <memory>
#include <utility>
#include <vector>  // IWYU pragma: keep

#include "absl/log/check.h"
#include "vulkan/vulkan_core.h"

#include "cuttlefish/host/libs/gpu/rgba_to_nv12.h"
#include "cuttlefish/host/libs/gpu/vulkan_handle.h"
#include "cuttlefish/host/libs/gpu/vulkan_loader.h"
#include "cuttlefish/host/libs/gpu/vulkan_resources.h"
#include "cuttlefish/host/libs/gpu/vulkan_video_context.h"
#include "cuttlefish/result/result.h"

namespace cuttlefish {
namespace {

// kRgbaToNv12Comp
#include "cuttlefish/host/libs/gpu/shaders/rgba_to_nv12.comp.inl"

// The encode input format may not support storage usage, so the shader writes
// single plane images in the format of each plane and they are copied in.
constexpr VkFormat kNv12LumaFormat = VK_FORMAT_R8_UNORM;
constexpr VkFormat kNv12ChromaFormat = VK_FORMAT_R8G8_UNORM;

// Side of the square workgroup the shader declares, in 2x2 pixel blocks.
constexpr uint32_t kNv12WorkgroupSize = 8;

// The plane images are written by the shader and read by the copy that
// follows it.
constexpr VkImageUsageFlags kNv12PlaneImageUsage =
    VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;

constexpr uint32_t kSourceBinding = 0;
constexpr uint32_t kLumaBinding = 1;
constexpr uint32_t kChromaBinding = 2;

// The push constant block the shader declares, field for field.
static_assert(offsetof(Nv12ConversionParams, visible_width) == 0);
static_assert(offsetof(Nv12ConversionParams, visible_height) == 4);
static_assert(offsetof(Nv12ConversionParams, coded_width) == 8);
static_assert(offsetof(Nv12ConversionParams, coded_height) == 12);
static_assert(offsetof(Nv12ConversionParams, source_stride_pixels) == 16);
static_assert(offsetof(Nv12ConversionParams, red_offset) == 20);
static_assert(sizeof(Nv12ConversionParams) == 24);

VkImageCreateInfo PlaneImageInfo(VkFormat format, VkExtent2D extent) {
  return VkImageCreateInfo{
      .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
      .pNext = nullptr,
      .flags = 0,
      .imageType = VK_IMAGE_TYPE_2D,
      .format = format,
      .extent = {extent.width, extent.height, 1},
      .mipLevels = 1,
      .arrayLayers = 1,
      .samples = VK_SAMPLE_COUNT_1_BIT,
      .tiling = VK_IMAGE_TILING_OPTIMAL,
      .usage = kNv12PlaneImageUsage,
      .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
      .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
  };
}

VkImageViewCreateInfo PlaneViewInfo(VkImage image, VkFormat format) {
  return VkImageViewCreateInfo{
      .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
      .pNext = nullptr,
      .flags = 0,
      .image = image,
      .viewType = VK_IMAGE_VIEW_TYPE_2D,
      .format = format,
      .components = {},
      .subresourceRange =
          {
              .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
              .baseMipLevel = 0,
              .levelCount = 1,
              .baseArrayLayer = 0,
              .layerCount = 1,
          },
  };
}

Result<UniqueVkHandle<VkShaderModule>> CreateShaderModule(
    const VulkanVideoContext& context) {
  const VulkanDeviceFunctions& vk = context.device_functions();
  const VkShaderModuleCreateInfo shader_info = {
      .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
      .pNext = nullptr,
      .flags = 0,
      .codeSize = kRgbaToNv12Comp.size(),
      .pCode = reinterpret_cast<const uint32_t*>(kRgbaToNv12Comp.data()),
  };
  VkShaderModule shader = VK_NULL_HANDLE;
  const VkResult res =
      vk.vkCreateShaderModule(context.device(), &shader_info, nullptr, &shader);
  CF_EXPECT_EQ(res, VK_SUCCESS, "vkCreateShaderModule failed");
  return UniqueVkHandle<VkShaderModule>(context.device(), shader,
                                        vk.vkDestroyShaderModule);
}

VkDescriptorSetLayoutBinding ComputeBinding(uint32_t binding,
                                            VkDescriptorType type) {
  return VkDescriptorSetLayoutBinding{
      .binding = binding,
      .descriptorType = type,
      .descriptorCount = 1,
      .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
      .pImmutableSamplers = nullptr,
  };
}

Result<UniqueVkHandle<VkDescriptorSetLayout>> CreateDescriptorSetLayout(
    const VulkanVideoContext& context) {
  const VulkanDeviceFunctions& vk = context.device_functions();
  const VkDescriptorSetLayoutBinding bindings[] = {
      ComputeBinding(kSourceBinding, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER),
      ComputeBinding(kLumaBinding, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE),
      ComputeBinding(kChromaBinding, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE),
  };
  const VkDescriptorSetLayoutCreateInfo layout_info = {
      .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
      .pNext = nullptr,
      .flags = 0,
      .bindingCount = static_cast<uint32_t>(std::size(bindings)),
      .pBindings = bindings,
  };
  VkDescriptorSetLayout layout = VK_NULL_HANDLE;
  const VkResult res = vk.vkCreateDescriptorSetLayout(
      context.device(), &layout_info, nullptr, &layout);
  CF_EXPECT_EQ(res, VK_SUCCESS, "vkCreateDescriptorSetLayout failed");
  return UniqueVkHandle<VkDescriptorSetLayout>(context.device(), layout,
                                               vk.vkDestroyDescriptorSetLayout);
}

Result<UniqueVkHandle<VkPipelineLayout>> CreatePipelineLayout(
    const VulkanVideoContext& context, VkDescriptorSetLayout set_layout) {
  const VulkanDeviceFunctions& vk = context.device_functions();
  const VkPushConstantRange push_constants = {
      .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
      .offset = 0,
      .size = sizeof(Nv12ConversionParams),
  };
  const VkPipelineLayoutCreateInfo pipeline_layout_info = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
      .pNext = nullptr,
      .flags = 0,
      .setLayoutCount = 1,
      .pSetLayouts = &set_layout,
      .pushConstantRangeCount = 1,
      .pPushConstantRanges = &push_constants,
  };
  VkPipelineLayout layout = VK_NULL_HANDLE;
  const VkResult res = vk.vkCreatePipelineLayout(
      context.device(), &pipeline_layout_info, nullptr, &layout);
  CF_EXPECT_EQ(res, VK_SUCCESS, "vkCreatePipelineLayout failed");
  return UniqueVkHandle<VkPipelineLayout>(context.device(), layout,
                                          vk.vkDestroyPipelineLayout);
}

Result<UniqueVkHandle<VkPipeline>> CreateComputePipeline(
    const VulkanVideoContext& context, VkShaderModule shader,
    VkPipelineLayout layout) {
  const VulkanDeviceFunctions& vk = context.device_functions();
  const VkComputePipelineCreateInfo pipeline_info = {
      .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
      .pNext = nullptr,
      .flags = 0,
      .stage =
          {
              .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
              .pNext = nullptr,
              .flags = 0,
              .stage = VK_SHADER_STAGE_COMPUTE_BIT,
              .module = shader,
              .pName = "main",
              .pSpecializationInfo = nullptr,
          },
      .layout = layout,
      .basePipelineHandle = VK_NULL_HANDLE,
      .basePipelineIndex = 0,
  };
  VkPipeline pipeline = VK_NULL_HANDLE;
  const VkResult res = vk.vkCreateComputePipelines(
      context.device(), VK_NULL_HANDLE, 1, &pipeline_info, nullptr, &pipeline);
  CF_EXPECT_EQ(res, VK_SUCCESS, "vkCreateComputePipelines failed");
  return UniqueVkHandle<VkPipeline>(context.device(), pipeline,
                                    vk.vkDestroyPipeline);
}

Result<UniqueVkHandle<VkDescriptorPool>> CreateDescriptorPool(
    const VulkanVideoContext& context) {
  const VulkanDeviceFunctions& vk = context.device_functions();
  const VkDescriptorPoolSize sizes[] = {
      {.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .descriptorCount = 1},
      {.type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, .descriptorCount = 2},
  };
  const VkDescriptorPoolCreateInfo pool_info = {
      .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
      .pNext = nullptr,
      .flags = 0,
      .maxSets = 1,
      .poolSizeCount = static_cast<uint32_t>(std::size(sizes)),
      .pPoolSizes = sizes,
  };
  VkDescriptorPool pool = VK_NULL_HANDLE;
  const VkResult res =
      vk.vkCreateDescriptorPool(context.device(), &pool_info, nullptr, &pool);
  CF_EXPECT_EQ(res, VK_SUCCESS, "vkCreateDescriptorPool failed");
  return UniqueVkHandle<VkDescriptorPool>(context.device(), pool,
                                          vk.vkDestroyDescriptorPool);
}

Result<VkDescriptorSet> AllocateDescriptorSet(
    const VulkanVideoContext& context, VkDescriptorPool pool,
    VkDescriptorSetLayout set_layout) {
  const VkDescriptorSetAllocateInfo allocate_info = {
      .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
      .pNext = nullptr,
      .descriptorPool = pool,
      .descriptorSetCount = 1,
      .pSetLayouts = &set_layout,
  };
  VkDescriptorSet set = VK_NULL_HANDLE;
  const VkResult res = context.device_functions().vkAllocateDescriptorSets(
      context.device(), &allocate_info, &set);
  CF_EXPECT_EQ(res, VK_SUCCESS, "vkAllocateDescriptorSets failed");
  return set;
}

VkWriteDescriptorSet DescriptorWrite(
    VkDescriptorSet set, uint32_t binding, VkDescriptorType type,
    const VkDescriptorImageInfo* image_info,
    const VkDescriptorBufferInfo* buffer_info) {
  return VkWriteDescriptorSet{
      .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
      .pNext = nullptr,
      .dstSet = set,
      .dstBinding = binding,
      .dstArrayElement = 0,
      .descriptorCount = 1,
      .descriptorType = type,
      .pImageInfo = image_info,
      .pBufferInfo = buffer_info,
      .pTexelBufferView = nullptr,
  };
}

}  // namespace

VkExtent2D Nv12DispatchGroupCount(uint32_t coded_width, uint32_t coded_height) {
  const uint32_t blocks_x = (coded_width + 1) / 2;
  const uint32_t blocks_y = (coded_height + 1) / 2;
  return VkExtent2D{
      .width = (blocks_x + kNv12WorkgroupSize - 1) / kNv12WorkgroupSize,
      .height = (blocks_y + kNv12WorkgroupSize - 1) / kNv12WorkgroupSize,
  };
}

Result<void> CheckNv12ConversionSupport(const VulkanVideoContext& context) {
  const VulkanInstanceFunctions& vk = context.instance_functions();

  const VkFormat formats[] = {kNv12LumaFormat, kNv12ChromaFormat};
  for (const VkFormat format : formats) {
    const VkPhysicalDeviceImageFormatInfo2 format_info = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_FORMAT_INFO_2,
        .pNext = nullptr,
        .format = format,
        .type = VK_IMAGE_TYPE_2D,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = kNv12PlaneImageUsage,
        .flags = 0,
    };
    VkImageFormatProperties2 properties = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_FORMAT_PROPERTIES_2,
        .pNext = nullptr,
        .imageFormatProperties = {},
    };
    const VkResult res = vk.vkGetPhysicalDeviceImageFormatProperties2(
        context.physical_device(), &format_info, &properties);
    CF_EXPECT_EQ(res, VK_SUCCESS,
                 "Driver refuses a storage image in format " << format);

    VkFormatProperties format_properties = {};
    vk.vkGetPhysicalDeviceFormatProperties(context.physical_device(), format,
                                           &format_properties);
    CF_EXPECT_NE(format_properties.optimalTilingFeatures &
                     VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT,
                 0u,
                 "Driver has no storage image support for format " << format);
  }
  return {};
}

Result<std::unique_ptr<VulkanNv12Converter>> VulkanNv12Converter::Create(
    std::shared_ptr<VulkanVideoContext> context, VkImage image,
    VkExtent2D coded_extent, VkBuffer source, VkDeviceSize source_size) {
  CHECK(context != nullptr);

  std::unique_ptr<VulkanNv12Converter> converter(
      new VulkanNv12Converter(std::move(context), image, coded_extent));
  CF_EXPECT(converter->CreatePlaneImages());
  CF_EXPECT(converter->CreatePipeline());
  CF_EXPECT(converter->CreateDescriptors(source, source_size));
  CF_EXPECT(converter->CreateCommandResources());
  return converter;
}

VulkanNv12Converter::VulkanNv12Converter(
    std::shared_ptr<VulkanVideoContext> context, VkImage image,
    VkExtent2D coded_extent)
    : context_(std::move(context)),
      image_(image),
      coded_extent_(coded_extent) {}

Result<void> VulkanNv12Converter::CreatePlaneImages() {
  const VkExtent2D chroma_extent = {
      .width = coded_extent_.width / 2,
      .height = coded_extent_.height / 2,
  };

  luma_image_ =
      CF_EXPECT(CreateVulkanImage(
                    *context_, PlaneImageInfo(kNv12LumaFormat, coded_extent_)),
                "Luma plane");
  luma_memory_ =
      CF_EXPECT(AllocateImageMemory(*context_, luma_image_.get(),
                                    VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 0),
                "Luma plane");
  luma_view_ = CF_EXPECT(
      CreateVulkanImageView(*context_,
                            PlaneViewInfo(luma_image_.get(), kNv12LumaFormat)),
      "Luma plane");

  chroma_image_ = CF_EXPECT(
      CreateVulkanImage(*context_,
                        PlaneImageInfo(kNv12ChromaFormat, chroma_extent)),
      "Chroma plane");
  chroma_memory_ =
      CF_EXPECT(AllocateImageMemory(*context_, chroma_image_.get(),
                                    VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 0),
                "Chroma plane");
  chroma_view_ = CF_EXPECT(
      CreateVulkanImageView(
          *context_, PlaneViewInfo(chroma_image_.get(), kNv12ChromaFormat)),
      "Chroma plane");
  return {};
}

Result<void> VulkanNv12Converter::CreatePipeline() {
  shader_ = CF_EXPECT(CreateShaderModule(*context_));
  descriptor_layout_ = CF_EXPECT(CreateDescriptorSetLayout(*context_));
  pipeline_layout_ =
      CF_EXPECT(CreatePipelineLayout(*context_, descriptor_layout_.get()));
  pipeline_ = CF_EXPECT(
      CreateComputePipeline(*context_, shader_.get(), pipeline_layout_.get()));
  return {};
}

Result<void> VulkanNv12Converter::CreateDescriptors(VkBuffer source,
                                                    VkDeviceSize source_size) {
  descriptor_pool_ = CF_EXPECT(CreateDescriptorPool(*context_));
  descriptor_set_ = CF_EXPECT(AllocateDescriptorSet(
      *context_, descriptor_pool_.get(), descriptor_layout_.get()));

  const VkDescriptorBufferInfo buffer_info = {
      .buffer = source,
      .offset = 0,
      .range = source_size,
  };
  const VkDescriptorImageInfo luma_info = {
      .sampler = VK_NULL_HANDLE,
      .imageView = luma_view_.get(),
      .imageLayout = VK_IMAGE_LAYOUT_GENERAL,
  };
  const VkDescriptorImageInfo chroma_info = {
      .sampler = VK_NULL_HANDLE,
      .imageView = chroma_view_.get(),
      .imageLayout = VK_IMAGE_LAYOUT_GENERAL,
  };
  const VkWriteDescriptorSet writes[] = {
      DescriptorWrite(descriptor_set_, kSourceBinding,
                      VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, nullptr, &buffer_info),
      DescriptorWrite(descriptor_set_, kLumaBinding,
                      VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, &luma_info, nullptr),
      DescriptorWrite(descriptor_set_, kChromaBinding,
                      VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, &chroma_info, nullptr),
  };
  context_->device_functions().vkUpdateDescriptorSets(
      context_->device(), static_cast<uint32_t>(std::size(writes)), writes, 0,
      nullptr);
  return {};
}

Result<void> VulkanNv12Converter::CreateCommandResources() {
  command_ = CF_EXPECT(
      CreateVulkanCommandBuffer(*context_, context_->compute_queue_family()),
      "Conversion command buffer");
  done_semaphore_ =
      CF_EXPECT(CreateVulkanSemaphore(*context_), "Conversion semaphore");
  return {};
}

Result<void> VulkanNv12Converter::Convert(const Nv12ConversionParams& params) {
  const VulkanDeviceFunctions& vk = context_->device_functions();

  CF_EXPECT_EQ(params.coded_width, coded_extent_.width,
               "Coded width differs from the converter's");
  CF_EXPECT_EQ(params.coded_height, coded_extent_.height,
               "Coded height differs from the converter's");

  CF_EXPECT(BeginOneTimeCommands(vk, command_.buffer), "Conversion");

  RecordConversion(params);
  RecordPlaneCopies();

  const VkResult res = vk.vkEndCommandBuffer(command_.buffer);
  CF_EXPECT_EQ(res, VK_SUCCESS, "vkEndCommandBuffer for conversion failed");

  CF_EXPECT(
      SubmitToComputeQueue(*context_, command_.buffer, done_semaphore_.get()),
      "Conversion");
  return {};
}

void VulkanNv12Converter::RecordConversion(const Nv12ConversionParams& params) {
  const VulkanDeviceFunctions& vk = context_->device_functions();

  // Every texel of both planes is written and then copied on, so no previous
  // contents matter and the old layouts can be discarded. The source scope
  // still has to name what the previous frame did on this queue, because a
  // layout transition is itself a write and would otherwise race the copy of
  // the frame before it.
  const VkPipelineStageFlags2 previous_stages =
      VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_2_COPY_BIT;
  const VkAccessFlags2 previous_accesses =
      VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT | VK_ACCESS_2_TRANSFER_READ_BIT |
      VK_ACCESS_2_TRANSFER_WRITE_BIT;
  const VulkanImageTransition to_storage = {
      .old_layout = VK_IMAGE_LAYOUT_UNDEFINED,
      .new_layout = VK_IMAGE_LAYOUT_GENERAL,
      .src_stage = previous_stages,
      .src_access = previous_accesses,
      .dst_stage = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
      .dst_access = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
  };
  const VulkanImageTransition to_copy_destination = {
      .old_layout = VK_IMAGE_LAYOUT_UNDEFINED,
      .new_layout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
      .src_stage = previous_stages,
      .src_access = previous_accesses,
      .dst_stage = VK_PIPELINE_STAGE_2_COPY_BIT,
      .dst_access = VK_ACCESS_2_TRANSFER_WRITE_BIT,
  };
  const VkImageMemoryBarrier2 pre_barriers[] = {
      ImageTransitionBarrier(luma_image_.get(), to_storage),
      ImageTransitionBarrier(chroma_image_.get(), to_storage),
      ImageTransitionBarrier(image_, to_copy_destination),
  };
  RecordImageBarriers(vk, command_.buffer, pre_barriers);

  vk.vkCmdBindPipeline(command_.buffer, VK_PIPELINE_BIND_POINT_COMPUTE,
                       pipeline_.get());
  vk.vkCmdBindDescriptorSets(command_.buffer, VK_PIPELINE_BIND_POINT_COMPUTE,
                             pipeline_layout_.get(), 0, 1, &descriptor_set_, 0,
                             nullptr);
  vk.vkCmdPushConstants(command_.buffer, pipeline_layout_.get(),
                        VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(params),
                        &params);

  const VkExtent2D groups =
      Nv12DispatchGroupCount(params.coded_width, params.coded_height);
  vk.vkCmdDispatch(command_.buffer, groups.width, groups.height, 1);
}

void VulkanNv12Converter::RecordPlaneCopies() {
  const VulkanDeviceFunctions& vk = context_->device_functions();

  const VulkanImageTransition to_copy_source = {
      .old_layout = VK_IMAGE_LAYOUT_GENERAL,
      .new_layout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
      .src_stage = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
      .src_access = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
      .dst_stage = VK_PIPELINE_STAGE_2_COPY_BIT,
      .dst_access = VK_ACCESS_2_TRANSFER_READ_BIT,
  };
  const VkImageMemoryBarrier2 copy_barriers[] = {
      ImageTransitionBarrier(luma_image_.get(), to_copy_source),
      ImageTransitionBarrier(chroma_image_.get(), to_copy_source),
  };
  RecordImageBarriers(vk, command_.buffer, copy_barriers);

  const VkImageCopy luma_copy = {
      .srcSubresource =
          {
              .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
              .mipLevel = 0,
              .baseArrayLayer = 0,
              .layerCount = 1,
          },
      .srcOffset = {0, 0, 0},
      .dstSubresource =
          {
              .aspectMask = VK_IMAGE_ASPECT_PLANE_0_BIT,
              .mipLevel = 0,
              .baseArrayLayer = 0,
              .layerCount = 1,
          },
      .dstOffset = {0, 0, 0},
      .extent = {coded_extent_.width, coded_extent_.height, 1},
  };
  vk.vkCmdCopyImage(command_.buffer, luma_image_.get(),
                    VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, image_,
                    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &luma_copy);
  const VkImageCopy chroma_copy = {
      .srcSubresource =
          {
              .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
              .mipLevel = 0,
              .baseArrayLayer = 0,
              .layerCount = 1,
          },
      .srcOffset = {0, 0, 0},
      .dstSubresource =
          {
              .aspectMask = VK_IMAGE_ASPECT_PLANE_1_BIT,
              .mipLevel = 0,
              .baseArrayLayer = 0,
              .layerCount = 1,
          },
      .dstOffset = {0, 0, 0},
      .extent = {coded_extent_.width / 2, coded_extent_.height / 2, 1},
  };
  vk.vkCmdCopyImage(command_.buffer, chroma_image_.get(),
                    VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, image_,
                    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &chroma_copy);
}

}  // namespace cuttlefish
