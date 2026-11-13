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

#include "cuttlefish/host/libs/gpu/vulkan_video_context.h"

#include <stdint.h>
#include <stdio.h>

#include <memory>
#include <optional>
#include <vector>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "vulkan/vulkan_core.h"

#include "cuttlefish/result/result.h"
#include "cuttlefish/result/result_matchers.h"

namespace cuttlefish {
namespace {

VkExtensionProperties MakeExtension(const char* name) {
  VkExtensionProperties extension = {};
  snprintf(extension.extensionName, sizeof(extension.extensionName), "%s",
           name);
  return extension;
}

VkQueueFamilyProperties MakeQueueFamily(VkQueueFlags flags) {
  return VkQueueFamilyProperties{.queueFlags = flags, .queueCount = 1};
}

TEST(VulkanVideoContextTest, DeviceHasBothQueuesWhenEncodeIsSupported) {
  if (!IsVulkanAv1EncodeSupported()) {
    GTEST_SKIP() << "No Vulkan AV1 encode device";
  }
  const Result<std::shared_ptr<VulkanVideoContext>> context =
      VulkanVideoContext::Get();
  ASSERT_THAT(context, IsOk());
  EXPECT_NE((*context)->device(), VK_NULL_HANDLE);
  EXPECT_NE((*context)->encode_queue(), VK_NULL_HANDLE);
  EXPECT_NE((*context)->compute_queue(), VK_NULL_HANDLE);
  EXPECT_GT((*context)->av1_capabilities().coded_picture_alignment.width, 0u);
  EXPECT_GT((*context)->av1_capabilities().coded_picture_alignment.height, 0u);
}

TEST(VulkanVideoContextTest, NoEncodeQueueFamilyIsRejected) {
  const std::vector<VkQueueFamilyProperties> families = {
      MakeQueueFamily(VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT),
      MakeQueueFamily(VK_QUEUE_TRANSFER_BIT),
      MakeQueueFamily(VK_QUEUE_VIDEO_DECODE_BIT_KHR),
  };
  EXPECT_EQ(FindVideoEncodeQueueFamily(families), std::nullopt);
}

TEST(VulkanVideoContextTest, EncodeQueueFamilyIsFound) {
  const std::vector<VkQueueFamilyProperties> families = {
      MakeQueueFamily(VK_QUEUE_GRAPHICS_BIT),
      MakeQueueFamily(VK_QUEUE_VIDEO_ENCODE_BIT_KHR | VK_QUEUE_TRANSFER_BIT),
  };
  EXPECT_EQ(FindVideoEncodeQueueFamily(families), std::optional<uint32_t>(1));
}

TEST(VulkanVideoContextTest, ComputeQueueFamilyIsSeparateFromEncode) {
  const std::vector<VkQueueFamilyProperties> families = {
      MakeQueueFamily(VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT),
      MakeQueueFamily(VK_QUEUE_VIDEO_ENCODE_BIT_KHR),
  };
  EXPECT_EQ(FindComputeQueueFamily(families), std::optional<uint32_t>(0));
  EXPECT_EQ(FindVideoEncodeQueueFamily(families), std::optional<uint32_t>(1));
}

TEST(VulkanVideoContextTest, NoComputeQueueFamilyIsFound) {
  const std::vector<VkQueueFamilyProperties> families = {
      MakeQueueFamily(VK_QUEUE_VIDEO_ENCODE_BIT_KHR),
  };
  EXPECT_EQ(FindComputeQueueFamily(families), std::nullopt);
}

TEST(VulkanVideoContextTest, MissingAv1ExtensionIsRejected) {
  const std::vector<VkExtensionProperties> extensions = {
      MakeExtension(VK_KHR_VIDEO_QUEUE_EXTENSION_NAME),
      MakeExtension(VK_KHR_VIDEO_ENCODE_QUEUE_EXTENSION_NAME),
  };
  EXPECT_FALSE(HasRequiredVideoEncodeExtensions(extensions));
}

TEST(VulkanVideoContextTest, DecodeOnlyDeviceIsRejected) {
  const std::vector<VkExtensionProperties> extensions = {
      MakeExtension(VK_KHR_VIDEO_QUEUE_EXTENSION_NAME),
      MakeExtension(VK_KHR_VIDEO_DECODE_QUEUE_EXTENSION_NAME),
  };
  EXPECT_FALSE(HasRequiredVideoEncodeExtensions(extensions));
}

TEST(VulkanVideoContextTest, NoExtensionsIsRejected) {
  EXPECT_FALSE(HasRequiredVideoEncodeExtensions({}));
}

TEST(VulkanVideoContextTest, AllRequiredExtensionsAccepted) {
  const std::vector<VkExtensionProperties> extensions = {
      MakeExtension(VK_KHR_SWAPCHAIN_EXTENSION_NAME),
      MakeExtension(VK_KHR_VIDEO_QUEUE_EXTENSION_NAME),
      MakeExtension(VK_KHR_VIDEO_ENCODE_QUEUE_EXTENSION_NAME),
      MakeExtension(VK_KHR_VIDEO_ENCODE_AV1_EXTENSION_NAME),
  };
  EXPECT_TRUE(HasRequiredVideoEncodeExtensions(extensions));
}

}  // namespace
}  // namespace cuttlefish
