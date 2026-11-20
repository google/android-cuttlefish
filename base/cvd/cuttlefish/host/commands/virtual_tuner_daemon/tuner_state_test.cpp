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

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

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
  EXPECT_TRUE(snapshot.station_present);
  EXPECT_EQ(snapshot.band, RadioBand::FM);
  EXPECT_EQ(snapshot.frequency_hz, 98500000u);
  EXPECT_EQ(snapshot.hd_subchannel, 1u);

  EXPECT_TRUE(state.IsPlaying());
  EXPECT_EQ(state.GetBand(), RadioBand::FM);
  EXPECT_EQ(state.GetFrequency(), 98500000u);
  EXPECT_EQ(state.GetSubchannel(), 1u);
}

TEST(TunerStateTest, DefaultStationsPopulatedAndSeekWraps) {
  TunerState state;
  std::vector<StationProgram> fm_stations = state.GetStations(RadioBand::FM);
  EXPECT_EQ(fm_stations.size(), 12u);
  std::vector<StationProgram> dab_stations = state.GetStations(RadioBand::DAB);
  EXPECT_EQ(dab_stations.size(), 9u);

  std::optional<StationProgram> hd_station =
      state.FindStation(RadioBand::FM, 98500000, 0);
  ASSERT_TRUE(hd_station.has_value());
  EXPECT_EQ(hd_station->program_name(), "98.5-1 South Bay's Classic Rock");
  EXPECT_EQ(hd_station->rds_ps(), "98.5-1");
  EXPECT_EQ(hd_station->rds_rt(), "98.5-1 RDS RT");
  EXPECT_EQ(hd_station->song_album(), "AlbumHd0");
  EXPECT_EQ(hd_station->genre(), "GenreHd0");
  EXPECT_EQ(hd_station->comment_short_description(), "CommentShort0");
  EXPECT_EQ(hd_station->comment_actual_text(), "CommentActual0");
  EXPECT_EQ(hd_station->commercial(), "Commercial0");
  ASSERT_EQ(hd_station->ufids_size(), 1);
  EXPECT_EQ(hd_station->ufids(0), "ufid0");

  std::optional<StationProgram> next =
      state.Seek(RadioBand::FM, /*start_frequency_hz=*/106100000,
                 /*start_hd_subchannel=*/0, /*direction_up=*/true,
                 /*skip_subchannel=*/false);
  ASSERT_TRUE(next.has_value());
  EXPECT_EQ(next->frequency_hz(), 94900000u);
  EXPECT_FALSE(state.IsPlaying());
}

TEST(TunerStateTest, UpsertAndRemoveStationUpdatesPresence) {
  TunerState state;
  state.SetTune(RadioBand::FM, 88500000, 0);
  EXPECT_FALSE(state.GetSnapshot().station_present);

  StationProgram info;
  info.set_band(RadioBand::FM);
  info.set_frequency_hz(88500000);
  info.set_program_name("Test FM");
  state.UpsertStation(info);
  EXPECT_TRUE(state.GetSnapshot().station_present);

  EXPECT_TRUE(state.RemoveStation(RadioBand::FM, 88500000, std::nullopt, 0));
  EXPECT_FALSE(state.GetSnapshot().station_present);
}

TEST(TunerStateTest, EnforcesStationTableCapAndClearsWavPathOnRemove) {
  TunerState state;
  state.SetStationWavPath(RadioBand::FM, 96500000, 0, "/tmp/koit.wav");
  state.SetTune(RadioBand::FM, 96500000, 0);
  EXPECT_EQ(state.GetSnapshot().station_wav_path, "/tmp/koit.wav");
  EXPECT_TRUE(state.RemoveStation(RadioBand::FM, 96500000, 0, 0));
  EXPECT_TRUE(state.GetSnapshot().station_wav_path.empty());

  // Out-of-range HD subchannel must not trigger shift UB.
  StationProgram high_hd;
  high_hd.set_band(RadioBand::FM);
  high_hd.set_frequency_hz(91100000);
  high_hd.set_hd_subchannel(32);
  EXPECT_TRUE(state.UpsertStation(high_hd).has_value());

  const size_t existing = state.GetStations(std::nullopt).size();
  for (size_t i = existing; i < kMaxStations; ++i) {
    StationProgram s;
    s.set_band(RadioBand::FM);
    s.set_frequency_hz(108100000 + static_cast<uint32_t>(i) * 1000);
    ASSERT_TRUE(state.UpsertStation(s).has_value());
  }
  StationProgram overflow;
  overflow.set_band(RadioBand::FM);
  overflow.set_frequency_hz(200000000);
  EXPECT_FALSE(state.UpsertStation(overflow).has_value());

  // Updating an existing station still succeeds at capacity.
  StationProgram existing_update;
  existing_update.set_band(RadioBand::FM);
  existing_update.set_frequency_hz(94900000);
  existing_update.set_program_name("Updated Wild 94.9");
  EXPECT_TRUE(state.UpsertStation(existing_update).has_value());
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
