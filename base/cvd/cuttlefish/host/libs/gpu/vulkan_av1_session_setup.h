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
#include <vector>

#include "vulkan/vulkan_core.h"

#include "cuttlefish/host/libs/gpu/vulkan_av1_encode_settings.h"
#include "cuttlefish/host/libs/gpu/vulkan_handle.h"
#include "cuttlefish/host/libs/gpu/vulkan_nv12_converter.h"
#include "cuttlefish/host/libs/gpu/vulkan_resources.h"
#include "cuttlefish/host/libs/gpu/vulkan_video_context.h"
#include "cuttlefish/result/result.h"

namespace cuttlefish {

// A video session and the memory bound to it.
struct VulkanAv1VideoSession {
  std::vector<UniqueVkHandle<VkDeviceMemory>> memory;
  UniqueVkHandle<VkVideoSessionKHR> session;
};

// The layered DPB image, with one view per slot.
struct VulkanAv1DpbImage {
  VulkanMemory memory;
  UniqueVkHandle<VkImage> image;
  std::vector<UniqueVkHandle<VkImageView>> views;
};

// The encode source image and its view.
struct VulkanAv1InputImage {
  VulkanMemory memory;
  UniqueVkHandle<VkImage> image;
  UniqueVkHandle<VkImageView> view;
};

struct VulkanAv1CommandResources {
  VulkanCommandBuffer encode;
  UniqueVkHandle<VkFence> fence;
  // On the compute queue family: the staging copy of the host conversion
  // path. The semaphore orders the copy before the encode.
  VulkanCommandBuffer copy;
  UniqueVkHandle<VkSemaphore> copy_semaphore;
};

// The Vulkan objects of one encode session. Members are destroyed in reverse
// declaration order, so each object goes before the ones it uses.
struct VulkanAv1SessionResources {
  VulkanAv1VideoSession video_session;
  UniqueVkHandle<VkVideoSessionParametersKHR> session_parameters;
  // The sequence header OBU the driver generated, as prepended to every key
  // frame.
  std::vector<uint8_t> sequence_header;
  VulkanAv1DpbImage dpb;
  VulkanAv1InputImage input;
  VulkanMappedBuffer staging;
  VulkanMappedBuffer output;
  UniqueVkHandle<VkQueryPool> query_pool;
  VulkanAv1CommandResources commands;
  // Null where the conversion runs on the host. The converter records copies
  // into the encode input image and reads the staging buffer, so it goes
  // first.
  std::unique_ptr<VulkanNv12Converter> converter;
};

// Creates every Vulkan object an encode session holds, and the conversion
// shader where the device can run it.
Result<VulkanAv1SessionResources> CreateVulkanAv1SessionResources(
    const std::shared_ptr<VulkanVideoContext>& context,
    const VulkanAv1EncodeSettings& settings);

}  // namespace cuttlefish
