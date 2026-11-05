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
