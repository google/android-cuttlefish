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

#include "cuttlefish/host/commands/virtual_tuner_daemon/tuner_state.h"

#include <gtest/gtest.h>

#include "cuttlefish/host/commands/virtual_tuner_daemon/VirtualTuner.pb.h"

namespace cuttlefish {
namespace virtualtuner {
namespace {

TEST(TunerStateTest, InitialStateIsUntuned) {
  TunerState state;
  TunerStateSnapshot snapshot = state.GetSnapshot();

  EXPECT_FALSE(snapshot.is_playing);
  EXPECT_EQ(snapshot.frequency_hz, 0u);
  EXPECT_FALSE(state.IsPlaying());
  EXPECT_EQ(state.GetFrequency(), 0u);
}

TEST(TunerStateTest, SetTuneUpdatesState) {
  TunerState state;
  state.SetTune(RadioBand::FM, 98500000, 1);

  TunerStateSnapshot snapshot = state.GetSnapshot();
  EXPECT_TRUE(snapshot.is_playing);
  EXPECT_EQ(snapshot.band, RadioBand::FM);
  EXPECT_EQ(snapshot.frequency_hz, 98500000u);
  EXPECT_EQ(snapshot.hd_subchannel, 1u);

  EXPECT_TRUE(state.IsPlaying());
  EXPECT_EQ(state.GetBand(), RadioBand::FM);
  EXPECT_EQ(state.GetFrequency(), 98500000u);
  EXPECT_EQ(state.GetSubchannel(), 1u);
}

TEST(TunerStateTest, StopResetsState) {
  TunerState state;
  state.SetTune(RadioBand::AM, 1000000, 0);
  EXPECT_TRUE(state.IsPlaying());

  state.Stop();
  TunerStateSnapshot snapshot = state.GetSnapshot();
  EXPECT_FALSE(snapshot.is_playing);
  EXPECT_EQ(snapshot.frequency_hz, 0u);
  EXPECT_FALSE(state.IsPlaying());
}

}  // namespace
}  // namespace virtualtuner
}  // namespace cuttlefish
