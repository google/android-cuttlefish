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

#include "cuttlefish/host/frontend/webrtc/audio_channel_matrix.h"

#include <gtest/gtest.h>

#include <vector>

namespace cuttlefish {
namespace {

TEST(AudioChannelMatrixTest, DuckAttenuatesDirectMapping) {
  const auto unducked = BuildChannelMixingMatrix(
      /*dst_channels=*/2, /*src_channels=*/2, /*volume=*/1.0f, /*fade=*/0.0f,
      /*balance=*/0.0f, /*ducked=*/false);
  const auto ducked = BuildChannelMixingMatrix(
      /*dst_channels=*/2, /*src_channels=*/2, /*volume=*/1.0f, /*fade=*/0.0f,
      /*balance=*/0.0f, /*ducked=*/true);

  ASSERT_EQ(unducked.size(), 2u);
  ASSERT_EQ(ducked.size(), 2u);
  EXPECT_FLOAT_EQ(unducked[0][0], 1.0f);
  EXPECT_FLOAT_EQ(unducked[1][1], 1.0f);
  EXPECT_FLOAT_EQ(ducked[0][0], 0.2f);
  EXPECT_FLOAT_EQ(ducked[0][1], 0.0f);
  EXPECT_FLOAT_EQ(ducked[1][0], 0.0f);
  EXPECT_FLOAT_EQ(ducked[1][1], 0.2f);
}

TEST(AudioChannelMatrixTest, DuckCombinesWithVolumeFadeAndBalance) {
  const auto stereo = BuildChannelMixingMatrix(
      /*dst_channels=*/2, /*src_channels=*/2, /*volume=*/0.5f, /*fade=*/-0.5f,
      /*balance=*/0.5f, /*ducked=*/true);
  // effective_volume = 0.5 * 0.2 = 0.1
  // front_gain = 0.5, left_gain = 0.5, right_gain = 1.0
  ASSERT_EQ(stereo.size(), 2u);
  EXPECT_FLOAT_EQ(stereo[0][0], 0.1f * 0.5f * 0.5f);
  EXPECT_FLOAT_EQ(stereo[0][1], 0.0f);
  EXPECT_FLOAT_EQ(stereo[1][0], 0.0f);
  EXPECT_FLOAT_EQ(stereo[1][1], 0.1f * 0.5f * 1.0f);

  const auto surround = BuildChannelMixingMatrix(
      /*dst_channels=*/2, /*src_channels=*/6, /*volume=*/0.5f, /*fade=*/0.5f,
      /*balance=*/-0.5f, /*ducked=*/true);
  // effective_volume = 0.1, front_gain = 1.0, rear_gain = 0.5,
  // left_gain = 1.0, right_gain = 0.5
  ASSERT_EQ(surround.size(), 2u);
  ASSERT_EQ(surround[0].size(), 6u);
  EXPECT_FLOAT_EQ(surround[0][0], 0.1f);              // FL
  EXPECT_FLOAT_EQ(surround[1][1], 0.1f * 0.5f);       // FR
  EXPECT_FLOAT_EQ(surround[0][4], 0.7071f * 0.05f);   // RL
  EXPECT_FLOAT_EQ(surround[1][5], 0.7071f * 0.025f);  // RR
}

}  // namespace
}  // namespace cuttlefish
