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

#include "cuttlefish/host/libs/gpu/vulkan_av1_encode_session.h"

#include <stdint.h>

#include <memory>
#include <mutex>
#include <utility>
#include <vector>

#include "absl/log/log.h"
#include "vulkan/vulkan_core.h"

#include "cuttlefish/host/libs/gpu/rgba_to_nv12.h"
#include "cuttlefish/host/libs/gpu/vulkan_av1_dpb.h"
#include "cuttlefish/host/libs/gpu/vulkan_av1_encode_commands.h"
#include "cuttlefish/host/libs/gpu/vulkan_av1_encode_settings.h"
#include "cuttlefish/host/libs/gpu/vulkan_av1_session_setup.h"
#include "cuttlefish/host/libs/gpu/vulkan_av1_syntax.h"
#include "cuttlefish/host/libs/gpu/vulkan_handle.h"
#include "cuttlefish/host/libs/gpu/vulkan_loader.h"
#include "cuttlefish/host/libs/gpu/vulkan_resources.h"
#include "cuttlefish/host/libs/gpu/vulkan_video_context.h"
#include "cuttlefish/result/result.h"

namespace cuttlefish {
namespace {

constexpr uint64_t kFenceTimeoutNs = 5'000'000'000;

// What the encode feedback query writes: one value per requested feedback
// flag in bit order, then the status.
struct EncodeFeedback {
  uint32_t offset;
  uint32_t bytes_written;
  VkQueryResultStatusKHR status;
};

}  // namespace

Result<std::unique_ptr<VulkanAv1EncodeSession>> VulkanAv1EncodeSession::Create(
    const VulkanAv1SessionConfig& config) {
  CF_EXPECT_GT(config.width, 0u, "Invalid frame width");
  CF_EXPECT_GT(config.height, 0u, "Invalid frame height");

  std::shared_ptr<VulkanVideoContext> context =
      CF_EXPECT(VulkanVideoContext::Get());
  const VulkanAv1EncodeSettings settings = CF_EXPECT(
      SelectVulkanAv1EncodeSettings(context->av1_capabilities(), config));
  VulkanAv1SessionResources resources =
      CF_EXPECT(CreateVulkanAv1SessionResources(context, settings));

  LOG(INFO) << "Vulkan AV1 encoder initialized: " << config.width << "x"
            << config.height << " coded as " << settings.coded_extent.width
            << "x" << settings.coded_extent.height
            << (settings.inter_frames_supported ? "" : ", intra only");
  return std::unique_ptr<VulkanAv1EncodeSession>(new VulkanAv1EncodeSession(
      std::move(context), settings, std::move(resources)));
}

VulkanAv1EncodeSession::VulkanAv1EncodeSession(
    std::shared_ptr<VulkanVideoContext> context,
    const VulkanAv1EncodeSettings& settings,
    VulkanAv1SessionResources resources)
    : context_(std::move(context)),
      settings_(settings),
      resources_(std::move(resources)),
      dpb_(settings.dpb_slots) {}

VulkanAv1EncodeSession::~VulkanAv1EncodeSession() {
  std::mutex& encode_mutex = context_->encode_queue_mutex();
  std::mutex& compute_mutex = context_->compute_queue_mutex();
  // vkDeviceWaitIdle needs every queue of the device externally synchronized.
  const std::lock_guard<std::mutex> encode_lock(encode_mutex);
  std::unique_lock<std::mutex> compute_lock(compute_mutex, std::defer_lock);
  if (&compute_mutex != &encode_mutex) {
    compute_lock.lock();
  }
  context_->device_functions().vkDeviceWaitIdle(context_->device());
}

Result<VulkanAv1EncodedFrame> VulkanAv1EncodeSession::EncodeFrame(
    const uint8_t* pixels, const Nv12ConversionParams& params,
    bool key_frame_requested) {
  const bool key_frame = key_frame_requested ||
                         !settings_.inter_frames_supported ||
                         !dpb_.ReferenceSlot().has_value();

  const VkSemaphore input_ready = CF_EXPECT(Upload(pixels, params));
  const VulkanAv1FrameCommands frame = {
      .commit =
          {
              .key_frame = key_frame,
              .order_hint = static_cast<uint8_t>(frame_count_ %
                                                 (1u << kAv1OrderHintBits)),
          },
      .first_frame = frame_count_ == 0,
  };
  CF_EXPECT(RecordVulkanAv1EncodeCommands(*context_, resources_, settings_,
                                          dpb_, frame));
  CF_EXPECT(SubmitAndWait(input_ready));
  const VulkanBitstreamRange range = CF_EXPECT(ReadBitstreamRange());
  CommitFrame(frame.commit);

  // The sequence header ahead of every key frame makes each one self
  // contained.
  const std::vector<uint8_t>& sequence_header = resources_.sequence_header;
  VulkanAv1EncodedFrame encoded = {.key_frame = key_frame};
  encoded.bitstream.reserve((key_frame ? sequence_header.size() : 0) +
                            range.size);
  if (key_frame) {
    encoded.bitstream.insert(encoded.bitstream.end(), sequence_header.begin(),
                             sequence_header.end());
  }
  const uint8_t* const frame_data =
      resources_.output.mapping.data() + range.offset;
  encoded.bitstream.insert(encoded.bitstream.end(), frame_data,
                           frame_data + range.size);
  return encoded;
}

Result<VkSemaphore> VulkanAv1EncodeSession::Upload(
    const uint8_t* pixels, const Nv12ConversionParams& params) {
  ConvertRgbaToNv12(pixels, params, resources_.staging.mapping.data());
  CF_EXPECT(FlushStaging());
  CF_EXPECT(SubmitStagingCopy());
  return resources_.commands.copy_semaphore.get();
}

Result<void> VulkanAv1EncodeSession::FlushStaging() {
  const VulkanMappedBuffer& staging = resources_.staging;
  if (staging.memory.host_coherent) {
    return {};
  }
  const VkMappedMemoryRange range = {
      .sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE,
      .pNext = nullptr,
      .memory = staging.memory.handle.get(),
      .offset = 0,
      .size = VK_WHOLE_SIZE,
  };
  const VkResult res = context_->device_functions().vkFlushMappedMemoryRanges(
      context_->device(), 1, &range);
  CF_EXPECT_EQ(res, VK_SUCCESS, "vkFlushMappedMemoryRanges failed");
  return {};
}

Result<void> VulkanAv1EncodeSession::SubmitStagingCopy() {
  CF_EXPECT(RecordVulkanAv1StagingCopy(*context_, resources_, settings_));
  CF_EXPECT(SubmitToComputeQueue(*context_, resources_.commands.copy.buffer,
                                 resources_.commands.copy_semaphore.get()),
            "Staging copy");
  return {};
}

Result<void> VulkanAv1EncodeSession::SubmitAndWait(VkSemaphore wait_semaphore) {
  const VulkanDeviceFunctions& vk = context_->device_functions();
  const VkFence fence = resources_.commands.fence.get();

  const VkCommandBufferSubmitInfo command_buffer_info = {
      .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO,
      .pNext = nullptr,
      .commandBuffer = resources_.commands.encode.buffer,
      .deviceMask = 0,
  };
  // The copy that filled the encode source image ran on the compute queue, so
  // the encode waits for it. Waiting at every stage keeps the layout
  // transition at the head of the command buffer ordered after the copy too.
  const VkSemaphoreSubmitInfo wait_info = {
      .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
      .pNext = nullptr,
      .semaphore = wait_semaphore,
      .value = 0,
      .stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
      .deviceIndex = 0,
  };
  const VkSubmitInfo2 submit_info = {
      .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
      .pNext = nullptr,
      .flags = 0,
      .waitSemaphoreInfoCount = 1,
      .pWaitSemaphoreInfos = &wait_info,
      .commandBufferInfoCount = 1,
      .pCommandBufferInfos = &command_buffer_info,
      .signalSemaphoreInfoCount = 0,
      .pSignalSemaphoreInfos = nullptr,
  };

  {
    const std::lock_guard<std::mutex> lock(context_->encode_queue_mutex());
    const VkResult res =
        vk.vkQueueSubmit2(context_->encode_queue(), 1, &submit_info, fence);
    CF_EXPECT_EQ(res, VK_SUCCESS, "vkQueueSubmit2 failed");
  }

  VkResult res = vk.vkWaitForFences(context_->device(), 1, &fence, VK_TRUE,
                                    kFenceTimeoutNs);
  CF_EXPECT_EQ(res, VK_SUCCESS, "vkWaitForFences failed");
  res = vk.vkResetFences(context_->device(), 1, &fence);
  CF_EXPECT_EQ(res, VK_SUCCESS, "vkResetFences failed");
  return {};
}

Result<VulkanBitstreamRange> VulkanAv1EncodeSession::ReadBitstreamRange() {
  const VulkanDeviceFunctions& vk = context_->device_functions();
  const VulkanMappedBuffer& output = resources_.output;

  EncodeFeedback feedback = {};
  VkResult res = vk.vkGetQueryPoolResults(
      context_->device(), resources_.query_pool.get(), 0, 1, sizeof(feedback),
      &feedback, sizeof(feedback),
      VK_QUERY_RESULT_WITH_STATUS_BIT_KHR | VK_QUERY_RESULT_WAIT_BIT);
  CF_EXPECT_EQ(res, VK_SUCCESS, "vkGetQueryPoolResults failed");
  CF_EXPECT_EQ(feedback.status, VK_QUERY_RESULT_STATUS_COMPLETE_KHR,
               "Encode query status");
  CF_EXPECT_GT(feedback.bytes_written, 0u, "Driver wrote an empty frame");
  // The frame starts where the driver says it does, which need not be the
  // start of the buffer.
  CF_EXPECT_LE(static_cast<uint64_t>(feedback.offset) + feedback.bytes_written,
               output.size,
               "Driver reported " << feedback.bytes_written
                                  << " bytes at offset " << feedback.offset);

  if (!output.memory.host_coherent) {
    const VkMappedMemoryRange range = {
        .sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE,
        .pNext = nullptr,
        .memory = output.memory.handle.get(),
        .offset = 0,
        .size = VK_WHOLE_SIZE,
    };
    res = vk.vkInvalidateMappedMemoryRanges(context_->device(), 1, &range);
    CF_EXPECT_EQ(res, VK_SUCCESS, "vkInvalidateMappedMemoryRanges failed");
  }
  return VulkanBitstreamRange{
      .offset = feedback.offset,
      .size = feedback.bytes_written,
  };
}

void VulkanAv1EncodeSession::CommitFrame(const VulkanFrameCommit& commit) {
  dpb_.Commit(commit.key_frame, commit.order_hint);
  frame_count_++;
}

}  // namespace cuttlefish
