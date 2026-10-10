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

#include "cuttlefish/host/libs/gpu/vulkan_av1_encode_commands.h"

#include <stddef.h>
#include <stdint.h>

#include <array>
#include <optional>
#include <vector>

#include "vulkan/vulkan_core.h"

#include "cuttlefish/host/libs/gpu/vulkan_av1_capabilities.h"
#include "cuttlefish/host/libs/gpu/vulkan_av1_dpb.h"
#include "cuttlefish/host/libs/gpu/vulkan_av1_encode_settings.h"
#include "cuttlefish/host/libs/gpu/vulkan_av1_rate_control.h"
#include "cuttlefish/host/libs/gpu/vulkan_av1_session_setup.h"
#include "cuttlefish/host/libs/gpu/vulkan_av1_syntax.h"
#include "cuttlefish/host/libs/gpu/vulkan_handle.h"
#include "cuttlefish/host/libs/gpu/vulkan_loader.h"
#include "cuttlefish/host/libs/gpu/vulkan_resources.h"
#include "cuttlefish/host/libs/gpu/vulkan_video_context.h"
#include "cuttlefish/result/result.h"

namespace cuttlefish {
namespace {

// Predictive frames reported as still pending where the driver requires a
// count. The stream has no fixed group structure, so more inter frames are
// always pending.
constexpr uint32_t kGopRemainingPredictive = 60;

std::vector<VkVideoPictureResourceInfoKHR> DpbPictureResources(
    const VulkanAv1DpbImage& dpb, VkExtent2D coded_extent) {
  std::vector<VkVideoPictureResourceInfoKHR> resources;
  for (const UniqueVkHandle<VkImageView>& view : dpb.views) {
    resources.push_back(VkVideoPictureResourceInfoKHR{
        .sType = VK_STRUCTURE_TYPE_VIDEO_PICTURE_RESOURCE_INFO_KHR,
        .pNext = nullptr,
        .codedOffset = {0, 0},
        .codedExtent = coded_extent,
        .baseArrayLayer = 0,
        .imageViewBinding = view.get(),
    });
  }
  return resources;
}

std::vector<VkVideoReferenceSlotInfoKHR> BeginSlots(
    const std::vector<VkVideoPictureResourceInfoKHR>& dpb_resources,
    std::optional<int32_t> reference_slot) {
  std::vector<VkVideoReferenceSlotInfoKHR> slots;
  for (size_t slot = 0; slot < dpb_resources.size(); slot++) {
    // Only the slot this frame predicts from is named as active; the one
    // being reconstructed into is bound as a resource and takes its slot
    // index from the setup slot of the encode command.
    slots.push_back(VkVideoReferenceSlotInfoKHR{
        .sType = VK_STRUCTURE_TYPE_VIDEO_REFERENCE_SLOT_INFO_KHR,
        .pNext = nullptr,
        .slotIndex =
            reference_slot == static_cast<int32_t>(slot) ? *reference_slot : -1,
        .pPictureResource = &dpb_resources[slot],
    });
  }
  return slots;
}

Av1PictureParams PictureParams(const VulkanAv1EncodeSettings& settings,
                               const Av1DpbPingPong& dpb,
                               const VulkanFrameCommit& commit) {
  return Av1PictureParams{
      .key_frame = commit.key_frame,
      .order_hint = commit.order_hint,
      .setup_slot = dpb.SetupSlot(commit.key_frame),
      .reference_slot = commit.key_frame ? std::nullopt : dpb.ReferenceSlot(),
      .width = settings.width,
      .height = settings.height,
      .coded_extent = settings.coded_extent,
      .q_index = settings.q_index,
      // The quantizer index is the encoder's to pick only where rate control
      // is switched off; otherwise it has to be zero.
      .constant_q_index =
          settings.rate_control.mode ==
                  VK_VIDEO_ENCODE_RATE_CONTROL_MODE_DISABLED_BIT_KHR
              ? settings.q_index
              : 0,
      .ref_order_hints = dpb.RefOrderHints(),
      .reference_frame_type = dpb.ReferenceFrameType(),
      .reference_order_hint = dpb.ReferenceOrderHint(),
  };
}

void RecordInputBarriers(const VulkanDeviceFunctions& vk,
                         VkCommandBuffer command_buffer,
                         const VulkanAv1SessionResources& resources,
                         const VulkanAv1EncodeSettings& settings,
                         bool first_frame) {
  if (first_frame) {
    RecordImageTransition(
        vk, command_buffer, resources.dpb.image.get(),
        {
            .old_layout = VK_IMAGE_LAYOUT_UNDEFINED,
            .new_layout = VK_IMAGE_LAYOUT_VIDEO_ENCODE_DPB_KHR,
            .src_stage = VK_PIPELINE_STAGE_2_NONE,
            .src_access = VK_ACCESS_2_NONE,
            .dst_stage = VK_PIPELINE_STAGE_2_VIDEO_ENCODE_BIT_KHR,
            .dst_access = VK_ACCESS_2_VIDEO_ENCODE_READ_BIT_KHR |
                          VK_ACCESS_2_VIDEO_ENCODE_WRITE_BIT_KHR,
            .layer_count = settings.dpb_slots,
        });
  }

  // The staging copy on the compute queue fills the image and leaves it as a
  // transfer destination. The semaphore that copy signals is what orders its
  // writes before this barrier, so the source scope here is empty.
  RecordImageTransition(
      vk, command_buffer, resources.input.image.get(),
      {
          .old_layout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
          .new_layout = VK_IMAGE_LAYOUT_VIDEO_ENCODE_SRC_KHR,
          .src_stage = VK_PIPELINE_STAGE_2_NONE,
          .src_access = VK_ACCESS_2_NONE,
          .dst_stage = VK_PIPELINE_STAGE_2_VIDEO_ENCODE_BIT_KHR,
          .dst_access = VK_ACCESS_2_VIDEO_ENCODE_READ_BIT_KHR,
      });
}

void RecordBeginCoding(
    const VulkanVideoContext& context, VkCommandBuffer command_buffer,
    const VulkanAv1SessionResources& resources,
    const VulkanAv1EncodeSettings& settings,
    const VulkanAv1FrameCommands& frame,
    const std::vector<VkVideoReferenceSlotInfoKHR>& begin_slots) {
  const VulkanRateControlChain active_chain(
      settings.rate_control, frame.active_bitrate_bps, frame.active_framerate);

  const VkVideoEncodeAV1GopRemainingFrameInfoKHR gop_remaining = {
      .sType = VK_STRUCTURE_TYPE_VIDEO_ENCODE_AV1_GOP_REMAINING_FRAME_INFO_KHR,
      .pNext = &active_chain.info(),
      .useGopRemainingFrames = VK_TRUE,
      .gopRemainingIntra = frame.commit.key_frame ? 1u : 0u,
      .gopRemainingPredictive = kGopRemainingPredictive,
      .gopRemainingBipredictive = 0,
  };
  const bool needs_gop_remaining =
      IsPacedRateControlMode(settings.rate_control.mode) &&
      context.av1_capabilities().requires_gop_remaining_frames;

  // The begin of a coding scope has to describe the rate control state the
  // session holds, so it carries the state as last applied, and nothing at
  // all before the first control command sets one.
  const void* begin_next = nullptr;
  if (!frame.first_frame) {
    begin_next = needs_gop_remaining
                     ? static_cast<const void*>(&gop_remaining)
                     : static_cast<const void*>(&active_chain.info());
  }
  const VkVideoBeginCodingInfoKHR begin_coding = {
      .sType = VK_STRUCTURE_TYPE_VIDEO_BEGIN_CODING_INFO_KHR,
      .pNext = begin_next,
      .flags = 0,
      .videoSession = resources.video_session.session.get(),
      .videoSessionParameters = resources.session_parameters.get(),
      .referenceSlotCount = static_cast<uint32_t>(begin_slots.size()),
      .pReferenceSlots = begin_slots.data(),
  };
  context.device_functions().vkCmdBeginVideoCodingKHR(command_buffer,
                                                      &begin_coding);
}

void RecordControlCommands(const VulkanDeviceFunctions& vk,
                           VkCommandBuffer command_buffer,
                           const VulkanAv1EncodeSettings& settings,
                           const VulkanAv1FrameCommands& frame) {
  if (frame.first_frame) {
    const VkVideoCodingControlInfoKHR reset_control = {
        .sType = VK_STRUCTURE_TYPE_VIDEO_CODING_CONTROL_INFO_KHR,
        .pNext = nullptr,
        .flags = VK_VIDEO_CODING_CONTROL_RESET_BIT_KHR,
    };
    vk.vkCmdControlVideoCodingKHR(command_buffer, &reset_control);

    // A reset leaves the session at quality level zero, and the session
    // parameters in use were created for the level below. The rate control
    // command that follows carries the state this level wants.
    if (settings.quality_level != 0) {
      const VkVideoEncodeQualityLevelInfoKHR quality_level_info = {
          .sType = VK_STRUCTURE_TYPE_VIDEO_ENCODE_QUALITY_LEVEL_INFO_KHR,
          .pNext = nullptr,
          .qualityLevel = settings.quality_level,
      };
      const VkVideoCodingControlInfoKHR quality_control = {
          .sType = VK_STRUCTURE_TYPE_VIDEO_CODING_CONTROL_INFO_KHR,
          .pNext = &quality_level_info,
          .flags = VK_VIDEO_CODING_CONTROL_ENCODE_QUALITY_LEVEL_BIT_KHR,
      };
      vk.vkCmdControlVideoCodingKHR(command_buffer, &quality_control);
    }
  }

  if (frame.first_frame ||
      frame.commit.bitrate_bps != frame.active_bitrate_bps ||
      frame.commit.framerate != frame.active_framerate) {
    const VulkanRateControlChain pending_chain(settings.rate_control,
                                               frame.commit.bitrate_bps,
                                               frame.commit.framerate);
    const VkVideoCodingControlInfoKHR rate_control_command = {
        .sType = VK_STRUCTURE_TYPE_VIDEO_CODING_CONTROL_INFO_KHR,
        .pNext = &pending_chain.info(),
        .flags = VK_VIDEO_CODING_CONTROL_ENCODE_RATE_CONTROL_BIT_KHR,
    };
    vk.vkCmdControlVideoCodingKHR(command_buffer, &rate_control_command);
  }
}

void RecordEncode(
    const VulkanDeviceFunctions& vk, VkCommandBuffer command_buffer,
    const VulkanAv1SessionResources& resources, const Av1PictureParams& params,
    const std::vector<VkVideoPictureResourceInfoKHR>& dpb_resources) {
  const VkQueryPool query_pool = resources.query_pool.get();

  const Av1PictureInfo picture(params);
  const VkVideoReferenceSlotInfoKHR setup_slot_info = {
      .sType = VK_STRUCTURE_TYPE_VIDEO_REFERENCE_SLOT_INFO_KHR,
      .pNext = &picture.setup_slot_info(),
      .slotIndex = params.setup_slot,
      .pPictureResource = &dpb_resources[params.setup_slot],
  };
  std::optional<VkVideoReferenceSlotInfoKHR> reference_slot_info;
  if (params.reference_slot.has_value()) {
    reference_slot_info = VkVideoReferenceSlotInfoKHR{
        .sType = VK_STRUCTURE_TYPE_VIDEO_REFERENCE_SLOT_INFO_KHR,
        .pNext = &picture.reference_slot_info(),
        .slotIndex = *params.reference_slot,
        .pPictureResource = &dpb_resources[*params.reference_slot],
    };
  }
  const VkVideoEncodeInfoKHR encode_info = {
      .sType = VK_STRUCTURE_TYPE_VIDEO_ENCODE_INFO_KHR,
      .pNext = &picture.info(),
      .flags = 0,
      .dstBuffer = resources.output.buffer.get(),
      .dstBufferOffset = 0,
      .dstBufferRange = resources.output.size,
      .srcPictureResource =
          {
              .sType = VK_STRUCTURE_TYPE_VIDEO_PICTURE_RESOURCE_INFO_KHR,
              .pNext = nullptr,
              .codedOffset = {0, 0},
              .codedExtent = params.coded_extent,
              .baseArrayLayer = 0,
              .imageViewBinding = resources.input.view.get(),
          },
      .pSetupReferenceSlot = &setup_slot_info,
      .referenceSlotCount = reference_slot_info.has_value() ? 1u : 0u,
      .pReferenceSlots = reference_slot_info.has_value()
                             ? &reference_slot_info.value()
                             : nullptr,
      .precedingExternallyEncodedBytes = 0,
  };

  vk.vkCmdBeginQuery(command_buffer, query_pool, 0, 0);
  vk.vkCmdEncodeVideoKHR(command_buffer, &encode_info);
  vk.vkCmdEndQuery(command_buffer, query_pool, 0);
}

// The two planes of an NV12 frame of `coded_extent` as they lie in a buffer,
// luma first and the interleaved chroma after it, each tightly packed.
std::array<VkBufferImageCopy, 2> Nv12PlaneCopyRegions(VkExtent2D coded_extent) {
  return {
      VkBufferImageCopy{
          .bufferOffset = 0,
          .bufferRowLength = 0,
          .bufferImageHeight = 0,
          .imageSubresource =
              {
                  .aspectMask = VK_IMAGE_ASPECT_PLANE_0_BIT,
                  .mipLevel = 0,
                  .baseArrayLayer = 0,
                  .layerCount = 1,
              },
          .imageOffset = {0, 0, 0},
          .imageExtent = {coded_extent.width, coded_extent.height, 1},
      },
      VkBufferImageCopy{
          .bufferOffset = static_cast<VkDeviceSize>(coded_extent.width) *
                          coded_extent.height,
          .bufferRowLength = 0,
          .bufferImageHeight = 0,
          .imageSubresource =
              {
                  .aspectMask = VK_IMAGE_ASPECT_PLANE_1_BIT,
                  .mipLevel = 0,
                  .baseArrayLayer = 0,
                  .layerCount = 1,
              },
          .imageOffset = {0, 0, 0},
          .imageExtent = {coded_extent.width / 2, coded_extent.height / 2, 1},
      },
  };
}

}  // namespace

Result<void> RecordVulkanAv1EncodeCommands(
    const VulkanVideoContext& context,
    const VulkanAv1SessionResources& resources,
    const VulkanAv1EncodeSettings& settings, const Av1DpbPingPong& dpb,
    const VulkanAv1FrameCommands& frame) {
  const VulkanDeviceFunctions& vk = context.device_functions();
  const VkCommandBuffer command_buffer = resources.commands.encode.buffer;

  CF_EXPECT(BeginOneTimeCommands(vk, command_buffer));
  RecordInputBarriers(vk, command_buffer, resources, settings,
                      frame.first_frame);
  vk.vkCmdResetQueryPool(command_buffer, resources.query_pool.get(), 0, 1);

  const Av1PictureParams picture_params =
      PictureParams(settings, dpb, frame.commit);
  const std::vector<VkVideoPictureResourceInfoKHR> dpb_resources =
      DpbPictureResources(resources.dpb, settings.coded_extent);
  const std::vector<VkVideoReferenceSlotInfoKHR> begin_slots =
      BeginSlots(dpb_resources, picture_params.reference_slot);

  RecordBeginCoding(context, command_buffer, resources, settings, frame,
                    begin_slots);
  RecordControlCommands(vk, command_buffer, settings, frame);
  RecordEncode(vk, command_buffer, resources, picture_params, dpb_resources);

  const VkVideoEndCodingInfoKHR end_coding = {
      .sType = VK_STRUCTURE_TYPE_VIDEO_END_CODING_INFO_KHR,
      .pNext = nullptr,
      .flags = 0,
  };
  vk.vkCmdEndVideoCodingKHR(command_buffer, &end_coding);

  const VkResult res = vk.vkEndCommandBuffer(command_buffer);
  CF_EXPECT_EQ(res, VK_SUCCESS, "vkEndCommandBuffer failed");
  return {};
}

Result<void> RecordVulkanAv1StagingCopy(
    const VulkanVideoContext& context,
    const VulkanAv1SessionResources& resources,
    const VulkanAv1EncodeSettings& settings) {
  const VulkanDeviceFunctions& vk = context.device_functions();
  const VkCommandBuffer command_buffer = resources.commands.copy.buffer;
  const VkImage image = resources.input.image.get();

  CF_EXPECT(BeginOneTimeCommands(vk, command_buffer), "Staging copy");

  // Every texel is overwritten, so the previous contents can be discarded.
  // The source scope names the previous frame's copy on this queue, because
  // a layout transition is itself a write.
  RecordImageTransition(vk, command_buffer, image,
                        {
                            .old_layout = VK_IMAGE_LAYOUT_UNDEFINED,
                            .new_layout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                            .src_stage = VK_PIPELINE_STAGE_2_COPY_BIT,
                            .src_access = VK_ACCESS_2_TRANSFER_WRITE_BIT,
                            .dst_stage = VK_PIPELINE_STAGE_2_COPY_BIT,
                            .dst_access = VK_ACCESS_2_TRANSFER_WRITE_BIT,
                        });

  const std::array<VkBufferImageCopy, 2> regions =
      Nv12PlaneCopyRegions(settings.coded_extent);
  vk.vkCmdCopyBufferToImage(command_buffer, resources.staging.buffer.get(),
                            image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                            static_cast<uint32_t>(regions.size()),
                            regions.data());

  const VkResult res = vk.vkEndCommandBuffer(command_buffer);
  CF_EXPECT_EQ(res, VK_SUCCESS,
               "vkEndCommandBuffer for the staging copy failed");
  return {};
}

}  // namespace cuttlefish
