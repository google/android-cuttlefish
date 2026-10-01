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

#include "cuttlefish/host/libs/gpu/vulkan_av1_session_setup.h"

#include <stddef.h>
#include <stdint.h>

#include <algorithm>
#include <memory>
#include <utility>
#include <vector>

#include "absl/log/log.h"
#include "vk_video/vulkan_video_codec_av1std.h"
#include "vk_video/vulkan_video_codec_av1std_encode.h"
#include "vulkan/vulkan_core.h"

#include "cuttlefish/host/libs/gpu/rgba_to_nv12.h"
#include "cuttlefish/host/libs/gpu/vulkan_av1_capabilities.h"
#include "cuttlefish/host/libs/gpu/vulkan_av1_encode_settings.h"
#include "cuttlefish/host/libs/gpu/vulkan_av1_syntax.h"
#include "cuttlefish/host/libs/gpu/vulkan_handle.h"
#include "cuttlefish/host/libs/gpu/vulkan_loader.h"
#include "cuttlefish/host/libs/gpu/vulkan_resources.h"
#include "cuttlefish/host/libs/gpu/vulkan_video_context.h"
#include "cuttlefish/result/result.h"

namespace cuttlefish {
namespace {

// What the encode source image is used for. The converted frame reaches it
// through a transfer from the staging buffer.
constexpr VkImageUsageFlags kInputImageUsage =
    VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_VIDEO_ENCODE_SRC_BIT_KHR;

Result<std::vector<UniqueVkHandle<VkDeviceMemory>>> BindVideoSessionMemory(
    const VulkanVideoContext& context, VkVideoSessionKHR session) {
  const VulkanDeviceFunctions& vk = context.device_functions();

  uint32_t requirement_count = 0;
  VkResult res = vk.vkGetVideoSessionMemoryRequirementsKHR(
      context.device(), session, &requirement_count, nullptr);
  CF_EXPECT_EQ(res, VK_SUCCESS,
               "vkGetVideoSessionMemoryRequirementsKHR failed");

  std::vector<VkVideoSessionMemoryRequirementsKHR> requirements(
      requirement_count,
      VkVideoSessionMemoryRequirementsKHR{
          .sType = VK_STRUCTURE_TYPE_VIDEO_SESSION_MEMORY_REQUIREMENTS_KHR});
  res = vk.vkGetVideoSessionMemoryRequirementsKHR(
      context.device(), session, &requirement_count, requirements.data());
  CF_EXPECT_EQ(res, VK_SUCCESS,
               "vkGetVideoSessionMemoryRequirementsKHR failed");

  std::vector<UniqueVkHandle<VkDeviceMemory>> memory;
  std::vector<VkBindVideoSessionMemoryInfoKHR> binds;
  binds.reserve(requirement_count);
  for (uint32_t i = 0; i < requirement_count; i++) {
    VulkanMemory allocated = CF_EXPECT(
        AllocateVulkanMemory(context, requirements[i].memoryRequirements, 0,
                             VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT));
    binds.push_back(VkBindVideoSessionMemoryInfoKHR{
        .sType = VK_STRUCTURE_TYPE_BIND_VIDEO_SESSION_MEMORY_INFO_KHR,
        .pNext = nullptr,
        .memoryBindIndex = requirements[i].memoryBindIndex,
        .memory = allocated.handle.get(),
        .memoryOffset = 0,
        .memorySize = requirements[i].memoryRequirements.size,
    });
    memory.emplace_back(std::move(allocated.handle));
  }

  if (!binds.empty()) {
    res = vk.vkBindVideoSessionMemoryKHR(context.device(), session,
                                         static_cast<uint32_t>(binds.size()),
                                         binds.data());
    CF_EXPECT_EQ(res, VK_SUCCESS, "vkBindVideoSessionMemoryKHR failed");
  }
  return memory;
}

Result<VulkanAv1VideoSession> CreateVideoSession(
    const VulkanVideoContext& context, const VulkanAv1EncodeSettings& settings,
    const Av1EncodeProfile& profile) {
  const VulkanDeviceFunctions& vk = context.device_functions();

  const VkExtensionProperties std_header_version = {
      .extensionName = VK_STD_VULKAN_VIDEO_CODEC_AV1_ENCODE_EXTENSION_NAME,
      .specVersion = VK_STD_VULKAN_VIDEO_CODEC_AV1_ENCODE_SPEC_VERSION,
  };
  const VkVideoEncodeAV1SessionCreateInfoKHR av1_session = {
      .sType = VK_STRUCTURE_TYPE_VIDEO_ENCODE_AV1_SESSION_CREATE_INFO_KHR,
      .pNext = nullptr,
      .useMaxLevel = VK_FALSE,
      .maxLevel = STD_VIDEO_AV1_LEVEL_2_0,
  };
  const VkVideoSessionCreateInfoKHR session_info = {
      .sType = VK_STRUCTURE_TYPE_VIDEO_SESSION_CREATE_INFO_KHR,
      .pNext = &av1_session,
      .queueFamilyIndex = context.encode_queue_family(),
      .flags = 0,
      .pVideoProfile = &profile.info(),
      .pictureFormat = kVulkanAv1EncodeFormat,
      .maxCodedExtent = settings.coded_extent,
      .referencePictureFormat = kVulkanAv1EncodeFormat,
      .maxDpbSlots = settings.dpb_slots,
      .maxActiveReferencePictures = 0,
      .pStdHeaderVersion = &std_header_version,
  };

  VkVideoSessionKHR session = VK_NULL_HANDLE;
  const VkResult res = vk.vkCreateVideoSessionKHR(
      context.device(), &session_info, nullptr, &session);
  CF_EXPECT_EQ(res, VK_SUCCESS, "vkCreateVideoSessionKHR failed");
  VulkanAv1VideoSession video_session = {
      .session = UniqueVkHandle<VkVideoSessionKHR>(context.device(), session,
                                                   vk.vkDestroyVideoSessionKHR),
  };
  video_session.memory = CF_EXPECT(BindVideoSessionMemory(context, session));
  return video_session;
}

Result<UniqueVkHandle<VkVideoSessionParametersKHR>> CreateSessionParameters(
    const VulkanVideoContext& context, const VulkanAv1EncodeSettings& settings,
    VkVideoSessionKHR session) {
  const VulkanDeviceFunctions& vk = context.device_functions();

  const Av1SequenceHeaderInfo sequence_header(settings.coded_extent);
  const VkVideoEncodeAV1SessionParametersCreateInfoKHR av1_parameters = {
      .sType =
          VK_STRUCTURE_TYPE_VIDEO_ENCODE_AV1_SESSION_PARAMETERS_CREATE_INFO_KHR,
      .pNext = nullptr,
      .pStdSequenceHeader = &sequence_header.sequence_header(),
      .pStdDecoderModelInfo = nullptr,
      .stdOperatingPointCount = 1,
      .pStdOperatingPoints = &sequence_header.operating_point(),
  };
  const VkVideoSessionParametersCreateInfoKHR parameters_info = {
      .sType = VK_STRUCTURE_TYPE_VIDEO_SESSION_PARAMETERS_CREATE_INFO_KHR,
      .pNext = &av1_parameters,
      .flags = 0,
      .videoSessionParametersTemplate = VK_NULL_HANDLE,
      .videoSession = session,
  };

  VkVideoSessionParametersKHR parameters = VK_NULL_HANDLE;
  const VkResult res = vk.vkCreateVideoSessionParametersKHR(
      context.device(), &parameters_info, nullptr, &parameters);
  CF_EXPECT_EQ(res, VK_SUCCESS, "vkCreateVideoSessionParametersKHR failed");
  return UniqueVkHandle<VkVideoSessionParametersKHR>(
      context.device(), parameters, vk.vkDestroyVideoSessionParametersKHR);
}

Result<std::vector<uint8_t>> FetchSequenceHeader(
    const VulkanVideoContext& context, VkVideoSessionParametersKHR parameters) {
  const VulkanDeviceFunctions& vk = context.device_functions();

  VkVideoEncodeSessionParametersFeedbackInfoKHR feedback = {
      .sType =
          VK_STRUCTURE_TYPE_VIDEO_ENCODE_SESSION_PARAMETERS_FEEDBACK_INFO_KHR,
      .pNext = nullptr,
      .hasOverrides = VK_FALSE,
  };
  const VkVideoEncodeSessionParametersGetInfoKHR get_info = {
      .sType = VK_STRUCTURE_TYPE_VIDEO_ENCODE_SESSION_PARAMETERS_GET_INFO_KHR,
      .pNext = nullptr,
      .videoSessionParameters = parameters,
  };

  size_t size = 0;
  VkResult res = vk.vkGetEncodedVideoSessionParametersKHR(
      context.device(), &get_info, &feedback, &size, nullptr);
  CF_EXPECT_EQ(res, VK_SUCCESS, "vkGetEncodedVideoSessionParametersKHR failed");
  CF_EXPECT_GT(size, 0u, "Driver returned an empty sequence header");

  std::vector<uint8_t> sequence_header(size, 0);
  res = vk.vkGetEncodedVideoSessionParametersKHR(
      context.device(), &get_info, &feedback, &size, sequence_header.data());
  CF_EXPECT_EQ(res, VK_SUCCESS, "vkGetEncodedVideoSessionParametersKHR failed");
  sequence_header.resize(size);

  VLOG(1) << "Vulkan AV1 sequence header: " << sequence_header.size()
          << " bytes, driver overrides " << (feedback.hasOverrides == VK_TRUE);
  return sequence_header;
}

Result<UniqueVkHandle<VkImageView>> CreateEncodeImageView(
    const VulkanVideoContext& context, VkImage image, VkImageViewType view_type,
    uint32_t layer) {
  const VkImageViewCreateInfo view_info = {
      .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
      .pNext = nullptr,
      .flags = 0,
      .image = image,
      .viewType = view_type,
      .format = kVulkanAv1EncodeFormat,
      .components = {},
      .subresourceRange =
          {
              .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
              .baseMipLevel = 0,
              .levelCount = 1,
              .baseArrayLayer = layer,
              .layerCount = 1,
          },
  };
  return CF_EXPECT(CreateVulkanImageView(context, view_info));
}

Result<VulkanAv1DpbImage> CreateDpbImage(
    const VulkanVideoContext& context, const VulkanAv1EncodeSettings& settings,
    const VkVideoProfileListInfoKHR& profile_list) {
  const uint32_t queue_family = context.encode_queue_family();

  // Layered DPB: one image, one array layer per slot. Legal everywhere, and
  // required by drivers that report no separate reference images.
  const VkImageCreateInfo image_info = {
      .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
      .pNext = &profile_list,
      .flags = 0,
      .imageType = VK_IMAGE_TYPE_2D,
      .format = kVulkanAv1EncodeFormat,
      .extent = {settings.coded_extent.width, settings.coded_extent.height, 1},
      .mipLevels = 1,
      .arrayLayers = settings.dpb_slots,
      .samples = VK_SAMPLE_COUNT_1_BIT,
      .tiling = VK_IMAGE_TILING_OPTIMAL,
      .usage = VK_IMAGE_USAGE_VIDEO_ENCODE_DPB_BIT_KHR,
      .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
      .queueFamilyIndexCount = 1,
      .pQueueFamilyIndices = &queue_family,
      .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
  };
  VulkanAv1DpbImage dpb;
  dpb.image = CF_EXPECT(CreateVulkanImage(context, image_info), "DPB image");
  dpb.memory = CF_EXPECT(AllocateImageMemory(
      context, dpb.image.get(), 0, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT));

  for (uint32_t slot = 0; slot < settings.dpb_slots; slot++) {
    dpb.views.emplace_back(
        CF_EXPECTF(CreateEncodeImageView(context, dpb.image.get(),
                                         VK_IMAGE_VIEW_TYPE_2D_ARRAY, slot),
                   "DPB slot {}", slot));
  }
  return dpb;
}

Result<VulkanAv1InputImage> CreateInputImage(
    const VulkanVideoContext& context, const VulkanAv1EncodeSettings& settings,
    const VkVideoProfileListInfoKHR& profile_list) {
  // The compute family fills the image and the encode family reads it, every
  // frame. Concurrent access costs a little on some hardware and saves an
  // ownership transfer on every frame.
  const uint32_t queue_families[2] = {context.encode_queue_family(),
                                      context.compute_queue_family()};
  const uint32_t queue_family_count =
      queue_families[0] != queue_families[1] ? 2 : 1;

  const VkImageCreateInfo image_info = {
      .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
      .pNext = &profile_list,
      .flags = 0,
      .imageType = VK_IMAGE_TYPE_2D,
      .format = kVulkanAv1EncodeFormat,
      .extent = {settings.coded_extent.width, settings.coded_extent.height, 1},
      .mipLevels = 1,
      .arrayLayers = 1,
      .samples = VK_SAMPLE_COUNT_1_BIT,
      .tiling = VK_IMAGE_TILING_OPTIMAL,
      .usage = kInputImageUsage,
      .sharingMode = queue_family_count > 1 ? VK_SHARING_MODE_CONCURRENT
                                            : VK_SHARING_MODE_EXCLUSIVE,
      .queueFamilyIndexCount = queue_family_count,
      .pQueueFamilyIndices = queue_families,
      .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
  };
  VulkanAv1InputImage input;
  input.image =
      CF_EXPECT(CreateVulkanImage(context, image_info), "Encode input image");
  input.memory = CF_EXPECT(AllocateImageMemory(
      context, input.image.get(), 0, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT));

  input.view = CF_EXPECT(CreateEncodeImageView(context, input.image.get(),
                                               VK_IMAGE_VIEW_TYPE_2D, 0),
                         "Encode input image view");
  return input;
}

Result<VulkanMappedBuffer> CreateStagingBuffer(
    const VulkanVideoContext& context,
    const VulkanAv1EncodeSettings& settings) {
  const uint32_t queue_family = context.compute_queue_family();
  const VkExtent2D extent = settings.coded_extent;

  // Holds the converted NV12 frame. Only the compute queue reads it.
  const VkBufferCreateInfo buffer_info = {
      .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
      .pNext = nullptr,
      .flags = 0,
      .size = Nv12FrameSize(extent.width, extent.height),
      .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
      .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
      .queueFamilyIndexCount = 1,
      .pQueueFamilyIndices = &queue_family,
  };
  return CF_EXPECT(CreateMappedBuffer(context, buffer_info), "Staging buffer");
}

Result<VulkanMappedBuffer> CreateBitstreamBuffer(
    const VulkanVideoContext& context, const VulkanAv1EncodeSettings& settings,
    const VkVideoProfileListInfoKHR& profile_list) {
  const uint32_t queue_family = context.encode_queue_family();

  // One uncompressed frame is far more than a compressed one needs, and the
  // driver reports how the range has to be aligned.
  const VkDeviceSize size =
      Nv12FrameSize(settings.coded_extent.width, settings.coded_extent.height);
  const VkDeviceSize alignment = std::max<VkDeviceSize>(
      1, context.av1_capabilities().min_bitstream_buffer_size_alignment);

  const VkBufferCreateInfo buffer_info = {
      .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
      .pNext = &profile_list,
      .flags = 0,
      .size = ((size + alignment - 1) / alignment) * alignment,
      .usage = VK_BUFFER_USAGE_VIDEO_ENCODE_DST_BIT_KHR,
      .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
      .queueFamilyIndexCount = 1,
      .pQueueFamilyIndices = &queue_family,
  };
  return CF_EXPECT(CreateMappedBuffer(context, buffer_info),
                   "Bitstream buffer");
}

Result<UniqueVkHandle<VkQueryPool>> CreateFeedbackQueryPool(
    const VulkanVideoContext& context, const Av1EncodeProfile& profile) {
  const VulkanDeviceFunctions& vk = context.device_functions();

  const VkQueryPoolVideoEncodeFeedbackCreateInfoKHR feedback_info = {
      .sType =
          VK_STRUCTURE_TYPE_QUERY_POOL_VIDEO_ENCODE_FEEDBACK_CREATE_INFO_KHR,
      .pNext = &profile.info(),
      .encodeFeedbackFlags =
          VK_VIDEO_ENCODE_FEEDBACK_BITSTREAM_BUFFER_OFFSET_BIT_KHR |
          VK_VIDEO_ENCODE_FEEDBACK_BITSTREAM_BYTES_WRITTEN_BIT_KHR,
  };
  CF_EXPECT_EQ(context.av1_capabilities().supported_encode_feedback_flags &
                   feedback_info.encodeFeedbackFlags,
               feedback_info.encodeFeedbackFlags,
               "Driver does not report the bitstream feedback the encoder "
               "needs");

  const VkQueryPoolCreateInfo query_pool_info = {
      .sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO,
      .pNext = &feedback_info,
      .flags = 0,
      .queryType = VK_QUERY_TYPE_VIDEO_ENCODE_FEEDBACK_KHR,
      .queryCount = 1,
      .pipelineStatistics = 0,
  };
  VkQueryPool query_pool = VK_NULL_HANDLE;
  const VkResult res = vk.vkCreateQueryPool(context.device(), &query_pool_info,
                                            nullptr, &query_pool);
  CF_EXPECT_EQ(res, VK_SUCCESS, "vkCreateQueryPool failed");
  return UniqueVkHandle<VkQueryPool>(context.device(), query_pool,
                                     vk.vkDestroyQueryPool);
}

Result<VulkanAv1CommandResources> CreateCommandResources(
    const VulkanVideoContext& context) {
  VulkanAv1CommandResources commands;
  commands.encode = CF_EXPECT(
      CreateVulkanCommandBuffer(context, context.encode_queue_family()));
  commands.copy = CF_EXPECT(
      CreateVulkanCommandBuffer(context, context.compute_queue_family()));
  commands.fence = CF_EXPECT(CreateVulkanFence(context));
  commands.copy_semaphore = CF_EXPECT(CreateVulkanSemaphore(context));
  return commands;
}

}  // namespace

Result<VulkanAv1SessionResources> CreateVulkanAv1SessionResources(
    const std::shared_ptr<VulkanVideoContext>& context,
    const VulkanAv1EncodeSettings& settings) {
  const Av1EncodeProfile profile;
  const VkVideoProfileListInfoKHR profile_list = {
      .sType = VK_STRUCTURE_TYPE_VIDEO_PROFILE_LIST_INFO_KHR,
      .pNext = nullptr,
      .profileCount = 1,
      .pProfiles = &profile.info(),
  };

  VulkanAv1SessionResources resources;
  resources.video_session =
      CF_EXPECT(CreateVideoSession(*context, settings, profile));
  resources.session_parameters = CF_EXPECT(CreateSessionParameters(
      *context, settings, resources.video_session.session.get()));
  resources.sequence_header = CF_EXPECT(
      FetchSequenceHeader(*context, resources.session_parameters.get()));
  resources.dpb = CF_EXPECT(CreateDpbImage(*context, settings, profile_list));
  resources.input =
      CF_EXPECT(CreateInputImage(*context, settings, profile_list));
  resources.staging = CF_EXPECT(CreateStagingBuffer(*context, settings));
  resources.output =
      CF_EXPECT(CreateBitstreamBuffer(*context, settings, profile_list));
  resources.query_pool = CF_EXPECT(CreateFeedbackQueryPool(*context, profile));
  resources.commands = CF_EXPECT(CreateCommandResources(*context));
  return resources;
}

}  // namespace cuttlefish
