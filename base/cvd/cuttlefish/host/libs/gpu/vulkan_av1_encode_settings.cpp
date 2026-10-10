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

#include "cuttlefish/host/libs/gpu/vulkan_av1_encode_settings.h"

#include <stdint.h>

#include <algorithm>

#include "vulkan/vulkan_core.h"

#include "cuttlefish/host/libs/gpu/vulkan_av1_capabilities.h"
#include "cuttlefish/host/libs/gpu/vulkan_av1_rate_control.h"
#include "cuttlefish/result/result.h"

namespace cuttlefish {
namespace {

// Two slots, one holding the reference and one taking the reconstructed
// picture, swapping every frame.
constexpr uint32_t kDpbSlots = 2;

// Bit 0 of singleReferenceNameMask, the only reference name this encoder
// uses.
constexpr uint32_t kLastFrameNameBit = 1;

// Middle of the qindex range. Only the constant quality mode encodes with it.
constexpr uint32_t kDefaultQIndex = 128;

uint32_t RoundUpTo(uint32_t value, uint32_t alignment) {
  if (alignment <= 1) {
    return value;
  }
  return ((value + alignment - 1) / alignment) * alignment;
}

Result<VkExtent2D> CodedExtent(const VulkanAv1EncodeCapabilities& capabilities,
                               uint32_t width, uint32_t height) {
  const VkExtent2D coded_extent =
      AlignedCodedExtent(width, height, capabilities.coded_picture_alignment);
  CF_EXPECT_LE(coded_extent.width, capabilities.max_coded_extent.width,
               "Coded width above the maximum coded extent");
  CF_EXPECT_LE(coded_extent.height, capabilities.max_coded_extent.height,
               "Coded height above the maximum coded extent");
  CF_EXPECT_GE(coded_extent.width, capabilities.min_coded_extent.width,
               "Coded width below the minimum coded extent");
  CF_EXPECT_GE(coded_extent.height, capabilities.min_coded_extent.height,
               "Coded height below the minimum coded extent");
  return coded_extent;
}

}  // namespace

VkExtent2D AlignedCodedExtent(uint32_t width, uint32_t height,
                              VkExtent2D alignment) {
  // 4:2:0 needs even dimensions whatever the driver asks for.
  return VkExtent2D{
      .width = RoundUpTo(RoundUpTo(width, alignment.width), 2),
      .height = RoundUpTo(RoundUpTo(height, alignment.height), 2),
  };
}

Result<VulkanAv1EncodeSettings> SelectVulkanAv1EncodeSettings(
    const VulkanAv1EncodeCapabilities& capabilities,
    const VulkanAv1SessionConfig& config) {
  const VkExtent2D coded_extent =
      CF_EXPECT(CodedExtent(capabilities, config.width, config.height));
  CF_EXPECT_LE(capabilities.min_q_index, capabilities.max_q_index,
               "Driver reports an empty qindex range");
  const uint32_t dpb_slots = std::min(kDpbSlots, capabilities.max_dpb_slots);
  CF_EXPECT_GT(dpb_slots, 0u, "Driver reports no DPB slots");

  const VkVideoEncodeRateControlModeFlagBitsKHR rate_control_mode =
      SelectRateControlMode(capabilities.rate_control_modes);
  CF_EXPECT(
      capabilities.max_rate_control_layers > 0 ||
          rate_control_mode ==
              VK_VIDEO_ENCODE_RATE_CONTROL_MODE_DISABLED_BIT_KHR ||
          rate_control_mode == VK_VIDEO_ENCODE_RATE_CONTROL_MODE_DEFAULT_KHR,
      "Driver accepts no rate control layer");

  return VulkanAv1EncodeSettings{
      .width = config.width,
      .height = config.height,
      .coded_extent = coded_extent,
      .q_index = std::clamp(kDefaultQIndex, capabilities.min_q_index,
                            capabilities.max_q_index),
      .dpb_slots = dpb_slots,
      .inter_frames_supported =
          dpb_slots >= kDpbSlots &&
          capabilities.max_single_reference_count > 0 &&
          capabilities.max_active_reference_pictures > 0 &&
          (capabilities.single_reference_name_mask & kLastFrameNameBit) != 0,
      .rate_control =
          {
              .mode = rate_control_mode,
              .max_bitrate_bps = capabilities.max_bitrate_bps,
              .min_q_index = capabilities.min_q_index,
              .max_q_index = capabilities.max_q_index,
              .vbv = DeriveVbvSettings(config.virtual_buffer_size_ms,
                                       config.initial_virtual_buffer_size_ms),
          },
      .quality_level = capabilities.max_quality_levels > 0
                           ? std::min(config.quality_level,
                                      capabilities.max_quality_levels - 1)
                           : 0,
  };
}

}  // namespace cuttlefish
