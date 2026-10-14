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

#include "gtest/gtest.h"
#include "vulkan/vulkan_core.h"

namespace cuttlefish {
namespace {

TEST(SelectRateControlModeTest, PrefersCbr) {
  const VkVideoEncodeRateControlModeFlagsKHR all =
      VK_VIDEO_ENCODE_RATE_CONTROL_MODE_DISABLED_BIT_KHR |
      VK_VIDEO_ENCODE_RATE_CONTROL_MODE_CBR_BIT_KHR |
      VK_VIDEO_ENCODE_RATE_CONTROL_MODE_VBR_BIT_KHR;

  EXPECT_EQ(SelectRateControlMode(all),
            VK_VIDEO_ENCODE_RATE_CONTROL_MODE_CBR_BIT_KHR);
}

TEST(SelectRateControlModeTest, FallsBackFromCbrToVbr) {
  EXPECT_EQ(
      SelectRateControlMode(VK_VIDEO_ENCODE_RATE_CONTROL_MODE_VBR_BIT_KHR |
                            VK_VIDEO_ENCODE_RATE_CONTROL_MODE_DISABLED_BIT_KHR),
      VK_VIDEO_ENCODE_RATE_CONTROL_MODE_VBR_BIT_KHR);
}

TEST(SelectRateControlModeTest, FallsBackFromCbrToConstantQIndex) {
  EXPECT_EQ(
      SelectRateControlMode(VK_VIDEO_ENCODE_RATE_CONTROL_MODE_DISABLED_BIT_KHR),
      VK_VIDEO_ENCODE_RATE_CONTROL_MODE_DISABLED_BIT_KHR);
}

TEST(SelectRateControlModeTest, UsesTheDriverDefaultWhenNothingIsOffered) {
  EXPECT_EQ(SelectRateControlMode(0),
            VK_VIDEO_ENCODE_RATE_CONTROL_MODE_DEFAULT_KHR);
}

TEST(DeriveVbvSettingsTest, KeepsExplicitValues) {
  const VulkanVbvSettings settings = DeriveVbvSettings(200, 100);
  EXPECT_EQ(settings.buffer_size_ms, 200u);
  EXPECT_EQ(settings.initial_size_ms, 100u);
}

TEST(DeriveVbvSettingsTest, KeepsTheInitialFullnessInsideTheBuffer) {
  const VulkanVbvSettings settings = DeriveVbvSettings(200, 5000);
  EXPECT_EQ(settings.buffer_size_ms, 200u);
  EXPECT_EQ(settings.initial_size_ms, 200u);
}

}  // namespace
}  // namespace cuttlefish
