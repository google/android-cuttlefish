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

#include "cuttlefish/host/libs/gpu/vulkan_av1_dpb.h"

#include <optional>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "vk_video/vulkan_video_codec_av1std.h"

namespace cuttlefish {
namespace {

using ::testing::Each;

TEST(Av1DpbPingPongTest, StartsWithNoReference) {
  const Av1DpbPingPong dpb(2);
  EXPECT_EQ(dpb.ReferenceSlot(), std::nullopt);
  EXPECT_EQ(dpb.SetupSlot(true), 0);
  EXPECT_EQ(dpb.SetupSlot(false), 0);
}

TEST(Av1DpbPingPongTest, AlternatesBetweenTheTwoSlots) {
  Av1DpbPingPong dpb(2);

  dpb.Commit(true, 0);
  EXPECT_EQ(dpb.ReferenceSlot(), 0);
  EXPECT_EQ(dpb.ReferenceFrameType(), STD_VIDEO_AV1_FRAME_TYPE_KEY);
  EXPECT_EQ(dpb.SetupSlot(false), 1);

  dpb.Commit(false, 1);
  EXPECT_EQ(dpb.ReferenceSlot(), 1);
  EXPECT_EQ(dpb.ReferenceOrderHint(), 1);
  EXPECT_EQ(dpb.ReferenceFrameType(), STD_VIDEO_AV1_FRAME_TYPE_INTER);
  EXPECT_EQ(dpb.SetupSlot(false), 0);

  dpb.Commit(false, 2);
  EXPECT_EQ(dpb.ReferenceSlot(), 0);
  EXPECT_EQ(dpb.SetupSlot(false), 1);
}

// A key frame refreshes every reference buffer; an inter frame refreshes the
// one that shares its index with the slot it wrote.
TEST(Av1DpbPingPongTest, TracksReferenceOrderHints) {
  Av1DpbPingPong dpb(2);

  dpb.Commit(true, 7);
  EXPECT_THAT(dpb.RefOrderHints(), Each(7));

  dpb.Commit(false, 8);
  EXPECT_EQ(dpb.RefOrderHints()[1], 8);
  EXPECT_EQ(dpb.RefOrderHints()[0], 7);

  dpb.Commit(false, 9);
  EXPECT_EQ(dpb.RefOrderHints()[0], 9);
  EXPECT_EQ(dpb.RefOrderHints()[1], 8);
}

TEST(Av1DpbPingPongTest, AKeyFrameStartsOverFromSlotZero) {
  Av1DpbPingPong dpb(2);
  dpb.Commit(true, 0);
  dpb.Commit(false, 1);
  EXPECT_EQ(dpb.ReferenceSlot(), 1);

  EXPECT_EQ(dpb.SetupSlot(true), 0);
  dpb.Commit(true, 2);
  EXPECT_EQ(dpb.ReferenceSlot(), 0);
  EXPECT_EQ(dpb.ReferenceFrameType(), STD_VIDEO_AV1_FRAME_TYPE_KEY);
  EXPECT_THAT(dpb.RefOrderHints(), Each(2));
}

// A single slot leaves no room for a ping-pong, so the setup slot never
// moves off zero.
TEST(Av1DpbPingPongTest, HandlesASingleSlot) {
  Av1DpbPingPong dpb(1);
  dpb.Commit(true, 0);
  EXPECT_EQ(dpb.SetupSlot(false), 0);
}

}  // namespace
}  // namespace cuttlefish
