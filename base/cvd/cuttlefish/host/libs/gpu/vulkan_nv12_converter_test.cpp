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

#include "cuttlefish/host/libs/gpu/vulkan_nv12_converter.h"

#include "gtest/gtest.h"
#include "vulkan/vulkan_core.h"

namespace cuttlefish {
namespace {

TEST(Nv12DispatchGroupCountTest, CoversEveryBlock) {
  // 1920x1088 is 960x544 blocks of 2x2, which is 120x68 workgroups of 8x8.
  const VkExtent2D groups = Nv12DispatchGroupCount(1920, 1088);
  EXPECT_EQ(groups.width, 120u);
  EXPECT_EQ(groups.height, 68u);
}

TEST(Nv12DispatchGroupCountTest, ExactMultipleAddsNoWorkgroup) {
  // 1280x720 is 640x360 blocks, which is 80 by 45 workgroups exactly.
  const VkExtent2D groups = Nv12DispatchGroupCount(1280, 720);
  EXPECT_EQ(groups.width, 80u);
  EXPECT_EQ(groups.height, 45u);
}

TEST(Nv12DispatchGroupCountTest, RoundsPartialWorkgroupsUp) {
  // 1240x568 is 620x284 blocks, so both dimensions need a partial workgroup.
  const VkExtent2D groups = Nv12DispatchGroupCount(1240, 568);
  EXPECT_EQ(groups.width, 78u);
  EXPECT_EQ(groups.height, 36u);
  EXPECT_GE(groups.width * 8 * 2, 1240u);
  EXPECT_GE(groups.height * 8 * 2, 568u);
}

}  // namespace
}  // namespace cuttlefish
