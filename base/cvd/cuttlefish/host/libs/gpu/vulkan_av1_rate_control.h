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

namespace cuttlefish {

// Coded picture buffer size and initial fullness in milliseconds of the
// target bitrate.
struct VulkanVbvSettings {
  uint32_t buffer_size_ms = 0;
  uint32_t initial_size_ms = 0;
};

// Picks the first of CBR, VBR and constant quantizer index that the driver
// reports. The last resort is the driver's own default, which every
// implementation supports.
VkVideoEncodeRateControlModeFlagBitsKHR SelectRateControlMode(
    VkVideoEncodeRateControlModeFlagsKHR supported);

// Keeps the initial fullness within the buffer.
VulkanVbvSettings DeriveVbvSettings(uint32_t buffer_size_ms,
                                    uint32_t initial_size_ms);

// Returns true for CBR and VBR, the modes that pace against a bitrate.
bool IsPacedRateControlMode(VkVideoEncodeRateControlModeFlagBitsKHR mode);

// What the rate control state of a session is built from.
struct VulkanRateControlSettings {
  VkVideoEncodeRateControlModeFlagBitsKHR mode =
      VK_VIDEO_ENCODE_RATE_CONTROL_MODE_DEFAULT_KHR;
  uint64_t max_bitrate_bps = 0;
  uint32_t min_q_index = 0;
  uint32_t max_q_index = 0;
  VulkanVbvSettings vbv;
};

// The rate control structures the driver reads, kept together because they
// point at each other and have to outlive the command that consumes them.
class VulkanRateControlChain {
 public:
  // Builds the rate control state for a target bitrate and frame rate.
  VulkanRateControlChain(const VulkanRateControlSettings& settings,
                         int32_t bitrate_bps, uint32_t framerate);

  VulkanRateControlChain(const VulkanRateControlChain&) = delete;
  VulkanRateControlChain& operator=(const VulkanRateControlChain&) = delete;

  const VkVideoEncodeRateControlInfoKHR& info() const { return info_; }

 private:
  VkVideoEncodeAV1RateControlLayerInfoKHR av1_layer_;
  VkVideoEncodeRateControlLayerInfoKHR layer_;
  VkVideoEncodeAV1RateControlInfoKHR av1_info_;
  VkVideoEncodeRateControlInfoKHR info_;
};

}  // namespace cuttlefish
