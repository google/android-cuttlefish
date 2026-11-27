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

#include "vulkan/vulkan_core.h"

#include "cuttlefish/host/libs/gpu/vulkan_av1_capabilities.h"
#include "cuttlefish/host/libs/gpu/vulkan_av1_rate_control.h"
#include "cuttlefish/result/result.h"

namespace cuttlefish {

// Rounds a frame size up to the alignment the driver reports as
// codedPictureAlignment. 1920x1080 becomes 1920x1088 where the alignment is
// 64x16.
VkExtent2D AlignedCodedExtent(uint32_t width, uint32_t height,
                              VkExtent2D alignment);

// What the caller asks of an encode session.
struct VulkanAv1SessionConfig {
  uint32_t width = 0;
  uint32_t height = 0;
  uint32_t virtual_buffer_size_ms = 0;
  uint32_t initial_virtual_buffer_size_ms = 0;
  uint32_t quality_level = 0;
};

// How a session encodes, settled against what the driver reports.
struct VulkanAv1EncodeSettings {
  uint32_t width = 0;
  uint32_t height = 0;
  VkExtent2D coded_extent = {};
  uint32_t q_index = 0;
  uint32_t dpb_slots = 0;
  // False where the driver offers no single reference prediction with
  // LAST_FRAME, or fewer than two DPB slots. Every frame is then a key frame.
  bool inter_frames_supported = false;
  VulkanRateControlSettings rate_control;
  uint32_t quality_level = 0;
};

// Fails where the driver cannot encode the configured size or reports
// unusable limits.
Result<VulkanAv1EncodeSettings> SelectVulkanAv1EncodeSettings(
    const VulkanAv1EncodeCapabilities& capabilities,
    const VulkanAv1SessionConfig& config);

}  // namespace cuttlefish
