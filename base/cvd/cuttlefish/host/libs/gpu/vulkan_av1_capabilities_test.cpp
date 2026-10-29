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

#include <stdio.h>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "vk_video/vulkan_video_codec_av1std.h"
#include "vk_video/vulkan_video_codec_av1std_encode.h"
#include "vulkan/vulkan_core.h"

#include "cuttlefish/result/result_matchers.h"

namespace cuttlefish {
namespace {

VulkanAv1EncodeCapabilities UsableCapabilities() {
  VulkanAv1EncodeCapabilities capabilities = {
      .std_header_version =
          {
              .specVersion = VK_STD_VULKAN_VIDEO_CODEC_AV1_ENCODE_SPEC_VERSION,
          },
      .input_formats = {VK_FORMAT_R8_UNORM, kVulkanAv1EncodeFormat},
      .dpb_formats = {kVulkanAv1EncodeFormat},
  };
  snprintf(capabilities.std_header_version.extensionName,
           sizeof(capabilities.std_header_version.extensionName), "%s",
           VK_STD_VULKAN_VIDEO_CODEC_AV1_ENCODE_EXTENSION_NAME);
  return capabilities;
}

TEST(Av1EncodeProfileTest, ChainsUsageAndAv1Profile) {
  const Av1EncodeProfile profile;
  const VkVideoProfileInfoKHR& info = profile.info();

  EXPECT_EQ(info.videoCodecOperation,
            VK_VIDEO_CODEC_OPERATION_ENCODE_AV1_BIT_KHR);
  EXPECT_EQ(info.chromaSubsampling, VK_VIDEO_CHROMA_SUBSAMPLING_420_BIT_KHR);
  EXPECT_EQ(info.lumaBitDepth, VK_VIDEO_COMPONENT_BIT_DEPTH_8_BIT_KHR);

  const VkVideoEncodeUsageInfoKHR* usage =
      static_cast<const VkVideoEncodeUsageInfoKHR*>(info.pNext);
  ASSERT_NE(usage, nullptr);
  EXPECT_EQ(usage->sType, VK_STRUCTURE_TYPE_VIDEO_ENCODE_USAGE_INFO_KHR);

  const VkVideoEncodeAV1ProfileInfoKHR* av1_profile =
      static_cast<const VkVideoEncodeAV1ProfileInfoKHR*>(usage->pNext);
  ASSERT_NE(av1_profile, nullptr);
  EXPECT_EQ(av1_profile->sType,
            VK_STRUCTURE_TYPE_VIDEO_ENCODE_AV1_PROFILE_INFO_KHR);
  EXPECT_EQ(av1_profile->stdProfile, STD_VIDEO_AV1_PROFILE_MAIN);
}

TEST(CheckVulkanAv1EncodeCapabilitiesTest, AcceptsNv12AndTheVendoredHeader) {
  EXPECT_THAT(CheckVulkanAv1EncodeCapabilities(UsableCapabilities()), IsOk());
}

TEST(CheckVulkanAv1EncodeCapabilitiesTest, RejectsMissingNv12Input) {
  VulkanAv1EncodeCapabilities capabilities = UsableCapabilities();
  capabilities.input_formats = {VK_FORMAT_R8_UNORM};
  EXPECT_THAT(CheckVulkanAv1EncodeCapabilities(capabilities), IsError());
}

TEST(CheckVulkanAv1EncodeCapabilitiesTest, RejectsMissingNv12References) {
  VulkanAv1EncodeCapabilities capabilities = UsableCapabilities();
  capabilities.dpb_formats = {};
  EXPECT_THAT(CheckVulkanAv1EncodeCapabilities(capabilities), IsError());
}

TEST(CheckVulkanAv1EncodeCapabilitiesTest, RejectsAnotherCodecHeader) {
  VulkanAv1EncodeCapabilities capabilities = UsableCapabilities();
  snprintf(capabilities.std_header_version.extensionName,
           sizeof(capabilities.std_header_version.extensionName), "%s",
           "VK_STD_vulkan_video_codec_h264_encode");
  EXPECT_THAT(CheckVulkanAv1EncodeCapabilities(capabilities), IsError());
}

TEST(CheckVulkanAv1EncodeCapabilitiesTest, RejectsAnOlderCodecHeader) {
  VulkanAv1EncodeCapabilities capabilities = UsableCapabilities();
  capabilities.std_header_version.specVersion =
      VK_STD_VULKAN_VIDEO_CODEC_AV1_ENCODE_SPEC_VERSION - 1;
  EXPECT_THAT(CheckVulkanAv1EncodeCapabilities(capabilities), IsError());
}

}  // namespace
}  // namespace cuttlefish
