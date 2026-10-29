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

#include <vector>

#include "vulkan/vulkan_core.h"

#include "cuttlefish/host/libs/gpu/vulkan_loader.h"
#include "cuttlefish/result/result.h"

namespace cuttlefish {

// The encode input and reference picture format: 8-bit 4:2:0 NV12.
constexpr VkFormat kVulkanAv1EncodeFormat = VK_FORMAT_G8_B8R8_2PLANE_420_UNORM;

// The AV1 encode video profile: 8 bit 4:2:0 MAIN, streaming use, low latency
// tuning.
class Av1EncodeProfile {
 public:
  Av1EncodeProfile();

  Av1EncodeProfile(const Av1EncodeProfile&) = delete;
  Av1EncodeProfile& operator=(const Av1EncodeProfile&) = delete;

  const VkVideoProfileInfoKHR& info() const { return profile_; }

 private:
  VkVideoEncodeAV1ProfileInfoKHR av1_profile_;
  VkVideoEncodeUsageInfoKHR usage_;
  VkVideoProfileInfoKHR profile_;
};

// The AV1 encode capabilities the encoder branches on. Implementations
// diverge on alignment, reference limits and DPB layout, so all of these are
// read from the driver, never assumed.
struct VulkanAv1EncodeCapabilities {
  VkDeviceSize min_bitstream_buffer_size_alignment = 0;
  VkExtent2D min_coded_extent = {};
  VkExtent2D max_coded_extent = {};
  uint32_t max_dpb_slots = 0;
  uint32_t max_active_reference_pictures = 0;
  // The AV1 codec header name and version the driver implements.
  VkExtensionProperties std_header_version = {};

  VkVideoEncodeRateControlModeFlagsKHR rate_control_modes = 0;
  uint32_t max_rate_control_layers = 0;
  // Ceiling for every bitrate handed to the driver.
  uint64_t max_bitrate_bps = 0;
  uint32_t max_quality_levels = 0;
  VkVideoEncodeFeedbackFlagsKHR supported_encode_feedback_flags = 0;

  // The coded extent is rounded up to `coded_picture_alignment`.
  VkExtent2D coded_picture_alignment = {};
  uint32_t max_single_reference_count = 0;
  uint32_t single_reference_name_mask = 0;
  uint32_t min_q_index = 0;
  uint32_t max_q_index = 0;
  bool requires_gop_remaining_frames = false;

  std::vector<VkFormat> input_formats;
  std::vector<VkFormat> dpb_formats;
};

// Asks the driver of `device` for its AV1 encode capabilities. Fails if the
// device cannot encode the AV1 profile at all.
Result<VulkanAv1EncodeCapabilities> QueryVulkanAv1EncodeCapabilities(
    const VulkanInstanceFunctions& funcs, VkPhysicalDevice device);

// Fails unless the encoder can use a device with these capabilities: NV12
// input and reference pictures, and a codec header at least as new as the
// vendored one.
Result<void> CheckVulkanAv1EncodeCapabilities(
    const VulkanAv1EncodeCapabilities& capabilities);

}  // namespace cuttlefish
