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

#include "cuttlefish/host/libs/gpu/vulkan_av1_capabilities.h"

#include <stdint.h>
#include <string.h>

#include <algorithm>
#include <ios>
#include <iterator>
#include <vector>

#include "absl/log/log.h"
#include "vk_video/vulkan_video_codec_av1std.h"
#include "vk_video/vulkan_video_codec_av1std_encode.h"
#include "vulkan/vulkan_core.h"

#include "cuttlefish/host/libs/gpu/vulkan_loader.h"
#include "cuttlefish/result/result.h"

namespace cuttlefish {
namespace {

Result<std::vector<VkFormat>> QueryVideoFormats(
    const VulkanInstanceFunctions& funcs, VkPhysicalDevice device,
    const VkVideoProfileInfoKHR& profile, VkImageUsageFlags usage) {
  const VkVideoProfileListInfoKHR profile_list = {
      .sType = VK_STRUCTURE_TYPE_VIDEO_PROFILE_LIST_INFO_KHR,
      .pNext = nullptr,
      .profileCount = 1,
      .pProfiles = &profile,
  };
  const VkPhysicalDeviceVideoFormatInfoKHR format_info = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VIDEO_FORMAT_INFO_KHR,
      .pNext = &profile_list,
      .imageUsage = usage,
  };

  uint32_t count = 0;
  VkResult res = funcs.vkGetPhysicalDeviceVideoFormatPropertiesKHR(
      device, &format_info, &count, nullptr);
  CF_EXPECT_EQ(res, VK_SUCCESS,
               "vkGetPhysicalDeviceVideoFormatPropertiesKHR failed");
  CF_EXPECT_GT(count, 0u,
               "No video format for image usage 0x" << std::hex << usage);

  std::vector<VkVideoFormatPropertiesKHR> properties(
      count, VkVideoFormatPropertiesKHR{
                 .sType = VK_STRUCTURE_TYPE_VIDEO_FORMAT_PROPERTIES_KHR});
  res = funcs.vkGetPhysicalDeviceVideoFormatPropertiesKHR(
      device, &format_info, &count, properties.data());
  CF_EXPECT_EQ(res, VK_SUCCESS,
               "vkGetPhysicalDeviceVideoFormatPropertiesKHR failed");
  properties.resize(count);

  std::vector<VkFormat> formats;
  std::ranges::transform(properties, std::back_inserter(formats),
                         &VkVideoFormatPropertiesKHR::format);
  return formats;
}

void LogCapabilities(const VulkanAv1EncodeCapabilities& capabilities) {
  VLOG(1) << "Vulkan AV1 encode: rate control modes 0x" << std::hex
          << capabilities.rate_control_modes << std::dec
          << ", coded picture alignment "
          << capabilities.coded_picture_alignment.width << "x"
          << capabilities.coded_picture_alignment.height << ", "
          << capabilities.max_active_reference_pictures
          << " active reference(s), max bitrate "
          << capabilities.max_bitrate_bps << ", single reference name mask 0x"
          << std::hex << capabilities.single_reference_name_mask << std::dec
          << ", max quality level " << capabilities.max_quality_levels
          << ", gop remaining frames required "
          << capabilities.requires_gop_remaining_frames << ", std header "
          << capabilities.std_header_version.extensionName << " version "
          << capabilities.std_header_version.specVersion;
  for (const VkFormat format : capabilities.input_formats) {
    VLOG(1) << "Vulkan AV1 encode: input format " << format;
  }
  for (const VkFormat format : capabilities.dpb_formats) {
    VLOG(1) << "Vulkan AV1 encode: DPB format " << format;
  }
}

}  // namespace

Av1EncodeProfile::Av1EncodeProfile()
    : av1_profile_{
          .sType = VK_STRUCTURE_TYPE_VIDEO_ENCODE_AV1_PROFILE_INFO_KHR,
          .pNext = nullptr,
          .stdProfile = STD_VIDEO_AV1_PROFILE_MAIN,
      },
      usage_{
          .sType = VK_STRUCTURE_TYPE_VIDEO_ENCODE_USAGE_INFO_KHR,
          .pNext = &av1_profile_,
          .videoUsageHints = VK_VIDEO_ENCODE_USAGE_STREAMING_BIT_KHR,
          .videoContentHints = VK_VIDEO_ENCODE_CONTENT_RENDERED_BIT_KHR,
          .tuningMode = VK_VIDEO_ENCODE_TUNING_MODE_LOW_LATENCY_KHR,
      },
      profile_{
          .sType = VK_STRUCTURE_TYPE_VIDEO_PROFILE_INFO_KHR,
          .pNext = &usage_,
          .videoCodecOperation = VK_VIDEO_CODEC_OPERATION_ENCODE_AV1_BIT_KHR,
          .chromaSubsampling = VK_VIDEO_CHROMA_SUBSAMPLING_420_BIT_KHR,
          .lumaBitDepth = VK_VIDEO_COMPONENT_BIT_DEPTH_8_BIT_KHR,
          .chromaBitDepth = VK_VIDEO_COMPONENT_BIT_DEPTH_8_BIT_KHR,
      } {}

Result<VulkanAv1EncodeCapabilities> QueryVulkanAv1EncodeCapabilities(
    const VulkanInstanceFunctions& funcs, VkPhysicalDevice device) {
  const Av1EncodeProfile profile;
  VkVideoEncodeAV1CapabilitiesKHR av1_capabilities = {
      .sType = VK_STRUCTURE_TYPE_VIDEO_ENCODE_AV1_CAPABILITIES_KHR,
      .pNext = nullptr,
  };
  VkVideoEncodeCapabilitiesKHR encode_capabilities = {
      .sType = VK_STRUCTURE_TYPE_VIDEO_ENCODE_CAPABILITIES_KHR,
      .pNext = &av1_capabilities,
  };
  VkVideoCapabilitiesKHR capabilities = {
      .sType = VK_STRUCTURE_TYPE_VIDEO_CAPABILITIES_KHR,
      .pNext = &encode_capabilities,
  };
  const VkResult res = funcs.vkGetPhysicalDeviceVideoCapabilitiesKHR(
      device, &profile.info(), &capabilities);
  CF_EXPECT_EQ(res, VK_SUCCESS,
               "vkGetPhysicalDeviceVideoCapabilitiesKHR failed");

  VulkanAv1EncodeCapabilities result = {
      .min_bitstream_buffer_size_alignment =
          capabilities.minBitstreamBufferSizeAlignment,
      .min_coded_extent = capabilities.minCodedExtent,
      .max_coded_extent = capabilities.maxCodedExtent,
      .max_dpb_slots = capabilities.maxDpbSlots,
      .max_active_reference_pictures = capabilities.maxActiveReferencePictures,
      .std_header_version = capabilities.stdHeaderVersion,
      .rate_control_modes = encode_capabilities.rateControlModes,
      .max_rate_control_layers = encode_capabilities.maxRateControlLayers,
      .max_bitrate_bps = encode_capabilities.maxBitrate,
      .max_quality_levels = encode_capabilities.maxQualityLevels,
      .supported_encode_feedback_flags =
          encode_capabilities.supportedEncodeFeedbackFlags,
      .coded_picture_alignment = av1_capabilities.codedPictureAlignment,
      .max_single_reference_count = av1_capabilities.maxSingleReferenceCount,
      .single_reference_name_mask = av1_capabilities.singleReferenceNameMask,
      .min_q_index = av1_capabilities.minQIndex,
      .max_q_index = av1_capabilities.maxQIndex,
      .requires_gop_remaining_frames =
          av1_capabilities.requiresGopRemainingFrames == VK_TRUE,
      .input_formats =
          CF_EXPECT(QueryVideoFormats(funcs, device, profile.info(),
                                      VK_IMAGE_USAGE_VIDEO_ENCODE_SRC_BIT_KHR)),
      .dpb_formats =
          CF_EXPECT(QueryVideoFormats(funcs, device, profile.info(),
                                      VK_IMAGE_USAGE_VIDEO_ENCODE_DPB_BIT_KHR)),
  };
  LogCapabilities(result);
  return result;
}

Result<void> CheckVulkanAv1EncodeCapabilities(
    const VulkanAv1EncodeCapabilities& capabilities) {
  CF_EXPECT(
      std::ranges::find(capabilities.input_formats, kVulkanAv1EncodeFormat) !=
          capabilities.input_formats.end(),
      "Driver does not accept NV12 encode input");
  CF_EXPECT(
      std::ranges::find(capabilities.dpb_formats, kVulkanAv1EncodeFormat) !=
          capabilities.dpb_formats.end(),
      "Driver does not accept NV12 reference pictures");

  CF_EXPECT_EQ(strcmp(capabilities.std_header_version.extensionName,
                      VK_STD_VULKAN_VIDEO_CODEC_AV1_ENCODE_EXTENSION_NAME),
               0,
               "Driver codec header is "
                   << capabilities.std_header_version.extensionName);
  CF_EXPECT_GE(capabilities.std_header_version.specVersion,
               VK_STD_VULKAN_VIDEO_CODEC_AV1_ENCODE_SPEC_VERSION,
               "Driver codec header is older than the vendored one");
  return {};
}

}  // namespace cuttlefish
