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

#include "cuttlefish/host/libs/gpu/vulkan_av1_rate_control.h"

#include <stdint.h>

#include <algorithm>

#include "vulkan/vulkan_core.h"

namespace cuttlefish {
namespace {

// Peak allowance over the target for the variable bitrate mode, in percent.
// The constant bitrate mode is required to have none.
constexpr uint64_t kVbrPeakPercent = 150;

VkVideoEncodeAV1RateControlLayerInfoKHR QIndexLayerInfo(
    const VulkanRateControlSettings& settings) {
  return VkVideoEncodeAV1RateControlLayerInfoKHR{
      .sType = VK_STRUCTURE_TYPE_VIDEO_ENCODE_AV1_RATE_CONTROL_LAYER_INFO_KHR,
      .pNext = nullptr,
      .useMinQIndex = VK_TRUE,
      .minQIndex = {settings.min_q_index, settings.min_q_index,
                    settings.min_q_index},
      .useMaxQIndex = VK_TRUE,
      .maxQIndex = {settings.max_q_index, settings.max_q_index,
                    settings.max_q_index},
      .useMaxFrameSize = VK_FALSE,
      .maxFrameSize = {0, 0, 0},
  };
}

VkVideoEncodeAV1RateControlInfoKHR Av1RateControlInfo() {
  return VkVideoEncodeAV1RateControlInfoKHR{
      .sType = VK_STRUCTURE_TYPE_VIDEO_ENCODE_AV1_RATE_CONTROL_INFO_KHR,
      .pNext = nullptr,
      .flags = 0,
      .gopFrameCount = 0,
      .keyFramePeriod = 0,
      .consecutiveBipredictiveFrameCount = 0,
      .temporalLayerCount = 1,
  };
}

}  // namespace

VkVideoEncodeRateControlModeFlagBitsKHR SelectRateControlMode(
    VkVideoEncodeRateControlModeFlagsKHR supported) {
  constexpr VkVideoEncodeRateControlModeFlagBitsKHR kPreference[] = {
      VK_VIDEO_ENCODE_RATE_CONTROL_MODE_CBR_BIT_KHR,
      VK_VIDEO_ENCODE_RATE_CONTROL_MODE_VBR_BIT_KHR,
      VK_VIDEO_ENCODE_RATE_CONTROL_MODE_DISABLED_BIT_KHR,
  };
  for (const VkVideoEncodeRateControlModeFlagBitsKHR mode : kPreference) {
    if ((supported & mode) != 0) {
      return mode;
    }
  }
  return VK_VIDEO_ENCODE_RATE_CONTROL_MODE_DEFAULT_KHR;
}

VulkanVbvSettings DeriveVbvSettings(uint32_t buffer_size_ms,
                                    uint32_t initial_size_ms) {
  return VulkanVbvSettings{
      .buffer_size_ms = buffer_size_ms,
      .initial_size_ms = std::min(initial_size_ms, buffer_size_ms),
  };
}

bool IsPacedRateControlMode(VkVideoEncodeRateControlModeFlagBitsKHR mode) {
  return mode == VK_VIDEO_ENCODE_RATE_CONTROL_MODE_CBR_BIT_KHR ||
         mode == VK_VIDEO_ENCODE_RATE_CONTROL_MODE_VBR_BIT_KHR;
}

VulkanRateControlChain::VulkanRateControlChain(
    const VulkanRateControlSettings& settings, int32_t bitrate_bps,
    uint32_t framerate)
    : av1_layer_(QIndexLayerInfo(settings)), av1_info_(Av1RateControlInfo()) {
  const bool paced = IsPacedRateControlMode(settings.mode);
  const uint64_t max_bitrate = std::max<uint64_t>(1, settings.max_bitrate_bps);
  const uint64_t average = std::clamp<uint64_t>(
      static_cast<uint64_t>(std::max(bitrate_bps, 1)), 1, max_bitrate);
  // The constant bitrate mode requires the peak to equal the target.
  const uint64_t peak =
      settings.mode == VK_VIDEO_ENCODE_RATE_CONTROL_MODE_VBR_BIT_KHR
          ? std::min<uint64_t>(average * kVbrPeakPercent / 100, max_bitrate)
          : average;

  layer_ = VkVideoEncodeRateControlLayerInfoKHR{
      .sType = VK_STRUCTURE_TYPE_VIDEO_ENCODE_RATE_CONTROL_LAYER_INFO_KHR,
      .pNext = &av1_layer_,
      .averageBitrate = average,
      .maxBitrate = peak,
      .frameRateNumerator = std::max(1u, framerate),
      .frameRateDenominator = 1,
  };
  info_ = VkVideoEncodeRateControlInfoKHR{
      .sType = VK_STRUCTURE_TYPE_VIDEO_ENCODE_RATE_CONTROL_INFO_KHR,
      .pNext = paced ? static_cast<const void*>(&av1_info_) : nullptr,
      .flags = 0,
      .rateControlMode = settings.mode,
      .layerCount = paced ? 1u : 0u,
      .pLayers = paced ? &layer_ : nullptr,
      .virtualBufferSizeInMs = paced ? settings.vbv.buffer_size_ms : 0,
      .initialVirtualBufferSizeInMs = paced ? settings.vbv.initial_size_ms : 0,
  };
}

}  // namespace cuttlefish
