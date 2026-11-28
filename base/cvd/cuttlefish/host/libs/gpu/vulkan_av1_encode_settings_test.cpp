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

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "vulkan/vulkan_core.h"

#include "cuttlefish/host/libs/gpu/vulkan_av1_capabilities.h"
#include "cuttlefish/result/result.h"
#include "cuttlefish/result/result_matchers.h"

namespace cuttlefish {
namespace {

VulkanAv1EncodeCapabilities FilledCapabilities() {
  return VulkanAv1EncodeCapabilities{
      .min_bitstream_buffer_size_alignment = 256,
      .min_coded_extent = {64, 64},
      .max_coded_extent = {4096, 4096},
      .max_dpb_slots = 8,
      .max_active_reference_pictures = 1,
      .rate_control_modes = VK_VIDEO_ENCODE_RATE_CONTROL_MODE_CBR_BIT_KHR |
                            VK_VIDEO_ENCODE_RATE_CONTROL_MODE_VBR_BIT_KHR,
      .max_rate_control_layers = 1,
      .max_bitrate_bps = 50000000,
      .max_quality_levels = 4,
      .coded_picture_alignment = {64, 16},
      .max_single_reference_count = 1,
      .single_reference_name_mask = 1,
      .min_q_index = 1,
      .max_q_index = 255,
  };
}

VulkanAv1SessionConfig Config1080p() {
  return VulkanAv1SessionConfig{
      .width = 1920,
      .height = 1080,
      .virtual_buffer_size_ms = 200,
      .initial_virtual_buffer_size_ms = 500,
      .quality_level = 9,
  };
}

TEST(AlignedCodedExtentTest, PadsHeightOnSixtyFourBySixtyFour) {
  const VkExtent2D extent = AlignedCodedExtent(1920, 1080, VkExtent2D{64, 64});
  EXPECT_EQ(extent.width, 1920u);
  EXPECT_EQ(extent.height, 1088u);
}

TEST(AlignedCodedExtentTest, PadsHeightOnSixtyFourBySixteen) {
  const VkExtent2D extent = AlignedCodedExtent(1920, 1080, VkExtent2D{64, 16});
  EXPECT_EQ(extent.width, 1920u);
  EXPECT_EQ(extent.height, 1088u);
}

TEST(AlignedCodedExtentTest, PadsBothAxes) {
  const VkExtent2D extent = AlignedCodedExtent(720, 480, VkExtent2D{64, 64});
  EXPECT_EQ(extent.width, 768u);
  EXPECT_EQ(extent.height, 512u);
}

TEST(AlignedCodedExtentTest, PadsHeightOnlyWhenWidthIsAligned) {
  const VkExtent2D extent = AlignedCodedExtent(1280, 720, VkExtent2D{64, 64});
  EXPECT_EQ(extent.width, 1280u);
  EXPECT_EQ(extent.height, 768u);
}

TEST(AlignedCodedExtentTest, KeepsAlreadyAlignedSizes) {
  const VkExtent2D extent = AlignedCodedExtent(1920, 1080, VkExtent2D{8, 2});
  EXPECT_EQ(extent.width, 1920u);
  EXPECT_EQ(extent.height, 1080u);
}

// 4:2:0 needs even dimensions whatever the driver reports.
TEST(AlignedCodedExtentTest, RoundsOddSizesToEven) {
  const VkExtent2D extent = AlignedCodedExtent(641, 361, VkExtent2D{1, 1});
  EXPECT_EQ(extent.width, 642u);
  EXPECT_EQ(extent.height, 362u);
}

TEST(SelectVulkanAv1EncodeSettingsTest, SettlesAgainstTheCapabilities) {
  const Result<VulkanAv1EncodeSettings> settings =
      SelectVulkanAv1EncodeSettings(FilledCapabilities(), Config1080p());
  ASSERT_THAT(settings, IsOk());

  EXPECT_EQ(settings->width, 1920u);
  EXPECT_EQ(settings->height, 1080u);
  EXPECT_EQ(settings->coded_extent.width, 1920u);
  EXPECT_EQ(settings->coded_extent.height, 1088u);
  EXPECT_EQ(settings->q_index, 128u);
  EXPECT_EQ(settings->dpb_slots, 2u);
  EXPECT_TRUE(settings->inter_frames_supported);
  EXPECT_EQ(settings->rate_control.mode,
            VK_VIDEO_ENCODE_RATE_CONTROL_MODE_CBR_BIT_KHR);
  EXPECT_EQ(settings->rate_control.max_bitrate_bps, 50000000u);
  EXPECT_EQ(settings->rate_control.min_q_index, 1u);
  EXPECT_EQ(settings->rate_control.max_q_index, 255u);
  EXPECT_EQ(settings->rate_control.vbv.buffer_size_ms, 200u);
  EXPECT_EQ(settings->rate_control.vbv.initial_size_ms, 200u);
  EXPECT_EQ(settings->quality_level, 3u);
}

TEST(SelectVulkanAv1EncodeSettingsTest, EncodesKeyFramesOnlyWithoutLastFrame) {
  VulkanAv1EncodeCapabilities capabilities = FilledCapabilities();
  capabilities.single_reference_name_mask = 0;

  const Result<VulkanAv1EncodeSettings> settings =
      SelectVulkanAv1EncodeSettings(capabilities, Config1080p());
  ASSERT_THAT(settings, IsOk());
  EXPECT_FALSE(settings->inter_frames_supported);
}

TEST(SelectVulkanAv1EncodeSettingsTest, RejectsFramesAboveTheMaximumExtent) {
  VulkanAv1EncodeCapabilities capabilities = FilledCapabilities();
  capabilities.max_coded_extent = {1280, 720};

  EXPECT_THAT(SelectVulkanAv1EncodeSettings(capabilities, Config1080p()),
              IsError());
}

TEST(SelectVulkanAv1EncodeSettingsTest, RejectsAnEmptyQIndexRange) {
  VulkanAv1EncodeCapabilities capabilities = FilledCapabilities();
  capabilities.min_q_index = 200;
  capabilities.max_q_index = 100;

  EXPECT_THAT(SelectVulkanAv1EncodeSettings(capabilities, Config1080p()),
              IsError());
}

}  // namespace
}  // namespace cuttlefish
