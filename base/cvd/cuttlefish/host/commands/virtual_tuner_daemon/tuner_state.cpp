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

#include <algorithm>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "cuttlefish/host/commands/virtual_tuner_daemon/VirtualTuner.pb.h"

namespace cuttlefish {
namespace virtualtuner {
namespace {

StationProgram MakeAmFmStation(RadioBand band, uint32_t frequency_khz,
                               std::string name, std::string artist,
                               std::string title) {
  StationProgram s;
  s.set_band(band);
  s.set_frequency_hz(frequency_khz * 1000);
  s.set_program_name(std::move(name));
  s.set_rds_ps(s.program_name());
  s.set_song_artist(std::move(artist));
  s.set_song_title(std::move(title));
  return s;
}

StationProgram MakeHdStation(RadioBand band, uint32_t station_id,
                             uint32_t subchannel, uint32_t frequency_khz,
                             std::string name, std::string rds_ps,
                             std::string short_name, std::string long_name,
                             int32_t subchannels_mask) {
  const uint32_t meta_idx = (band == RadioBand::AM) ? 1u : subchannel;
  const std::string suffix = std::to_string(meta_idx);
  StationProgram s;
  s.set_band(band);
  s.set_frequency_hz(frequency_khz * 1000);
  s.set_hd_subchannel(subchannel);
  s.set_hd_station_id(station_id);
  s.set_program_name(std::move(name));
  s.set_rds_rt(rds_ps + " RDS RT");
  s.set_rds_ps(std::move(rds_ps));
  s.set_song_artist("ArtistHd" + suffix);
  s.set_song_title("TitleHd" + suffix);
  s.set_song_album("AlbumHd" + suffix);
  s.set_genre("GenreHd" + suffix);
  s.set_comment_short_description("CommentShort" + suffix);
  s.set_comment_actual_text("CommentActual" + suffix);
  s.set_commercial("Commercial" + suffix);
  s.add_ufids("ufid" + suffix);
  s.set_hd_station_name_short(std::move(short_name));
  s.set_hd_station_name_long(std::move(long_name));
  s.set_hd_subchannels_available(subchannels_mask);
  return s;
}

StationProgram MakeDabStation(uint64_t sid_ext, uint32_t ensemble,
                              uint32_t frequency_khz, std::string name,
                              std::string artist, std::string title) {
  StationProgram s;
  s.set_band(RadioBand::DAB);
  s.set_frequency_hz(frequency_khz * 1000);
  s.set_dab_sid_ext(sid_ext);
  s.set_dab_ensemble(ensemble);
  s.set_program_name(std::move(name));
  s.set_song_artist(std::move(artist));
  s.set_song_title(std::move(title));
  return s;
}

std::vector<StationProgram> DefaultStations() {
  return {
      // AM stations
      MakeAmFmStation(RadioBand::AM, 560, "Talk Radio 560 KSFO", "Artist560",
                      "Title560"),
      MakeAmFmStation(RadioBand::AM, 680, "KNBR 680", "Artist680", "Title680"),
      MakeHdStation(RadioBand::AM, 0xB0000001u, 0, 1170, "KLOK", "KLOK",
                    "KLOK-AM", "KLOK Radio", 0b1),
      // FM analog stations
      MakeAmFmStation(RadioBand::FM, 94900, "Wild 94.9", "Drake ft. Rihanna",
                      "Too Good"),
      MakeAmFmStation(RadioBand::FM, 96500, "KOIT", "Celine Dion",
                      "All By Myself"),
      MakeAmFmStation(RadioBand::FM, 97300, "Alice@97.3", "Drops of Jupiter",
                      "Train"),
      MakeAmFmStation(RadioBand::FM, 99700, "99.7 Now!", "The Chainsmokers",
                      "Closer"),
      MakeAmFmStation(RadioBand::FM, 101300, "101-3 KISS-FM",
                      "Justin Timberlake", "Rock Your Body"),
      MakeAmFmStation(RadioBand::FM, 103700, "iHeart80s @ 103.7",
                      "Michael Jackson", "Billie Jean"),
      MakeAmFmStation(RadioBand::FM, 106100, "106 KMEL", "Drake",
                      "Marvins Room"),
      // FM HD Radio stations
      MakeHdStation(RadioBand::FM, 0xA0000001u, 0, 97700, "K-LOVE", "K-LOVE",
                    "KLOVE-FM", "K-LOVE Radio", 0b111),
      MakeHdStation(RadioBand::FM, 0xA0000001u, 1, 97700, "Air1", "Air1",
                    "Air1-FM", "Air1 Radio", 0b111),
      MakeHdStation(RadioBand::FM, 0xA0000001u, 2, 97700, "K-LOVE Classics",
                    "K-LOVE Classics", "K-CLASSIC", "K-LOVE Classics Radio",
                    0b111),
      MakeHdStation(RadioBand::FM, 0xA0000001u, 0, 98500,
                    "98.5-1 South Bay's Classic Rock", "98.5-1", "KUFX-FM",
                    "98.5 K-FOX", 0b11),
      MakeHdStation(RadioBand::FM, 0xA0000001u, 1, 98500,
                    "Highway 1 - Different", "Highway 1", "HWY1-FM",
                    "Highway 1 Radio", 0b11),
      // DAB stations
      MakeDabStation(0x0E10000CCE7ULL, 0xC19Du, 218640,
                     "Absolute Radio Country", "ArtistCountry1",
                     "TitleCountry1"),
      MakeDabStation(0x0E10000CCE7ULL, 0xC1A0u, 218640,
                     "Absolute Radio Country", "ArtistCountry2",
                     "TitleCountry2"),
      MakeDabStation(0x0E10000C1C0ULL, 0xC181u, 222064, "Absolute Radio",
                     "Coldplay", "Clocks"),
      MakeDabStation(0x0E10000C21AULL, 0xC181u, 222064, "Classic FM",
                     "Jean Sibelius", "Andante Festivo"),
      MakeDabStation(0x0E10000C1C0ULL, 0xC181u, 223936, "Absolute Radio",
                     "Coldplay", "Clocks"),
      MakeDabStation(0x0E10000C221ULL, 0xCE15u, 225648, "BBC Radio 1", "Khalid",
                     "Talk"),
      MakeDabStation(0x0E10000C222ULL, 0xCE15u, 225648, "BBC Radio 2", "Khalid",
                     "Talk"),
      MakeDabStation(0xE10000C224ULL, 0xCE15u, 225648, "BBC Radio 4",
                     "ArtistBBC1", "TitleCountry1"),
      MakeDabStation(0x1E10000C224ULL, 0xCE15u, 225648, "BBC Radio 4 LW",
                     "ArtistBBC2", "TitleCountry2"),
  };
}

}  // namespace

TunerState::TunerState() {
  for (StationProgram& station : DefaultStations()) {
    const StationKey key{station.band(), station.frequency_hz(),
                         station.hd_subchannel(), station.dab_sid_ext(),
                         station.dab_ensemble()};
    stations_.emplace(key, std::move(station));
  }
}

void TunerState::SetTune(RadioBand band, uint32_t frequency_hz,
                         uint32_t hd_subchannel) {
  std::lock_guard<std::mutex> lock(mutex_);
  state_.band = band;
  state_.frequency_hz = frequency_hz;
  state_.hd_subchannel = hd_subchannel;
  state_.is_playing = true;
  current_station_ = FindStationLocked(band, frequency_hz, hd_subchannel);
  state_.station_present = current_station_.has_value();
  auto wav_it = station_wav_samples_.find(
      StationKey{band, frequency_hz, hd_subchannel, 0});
  state_.station_wav_samples =
      (wav_it != station_wav_samples_.end()) ? wav_it->second : nullptr;
}

void TunerState::Stop() {
  std::lock_guard<std::mutex> lock(mutex_);
  state_.frequency_hz = 0;
  state_.is_playing = false;
  state_.station_present = false;
  state_.station_wav_samples.reset();
  current_station_.reset();
}

void TunerState::SetStationWav(RadioBand band, uint32_t frequency_hz,
                               uint32_t hd_subchannel,
                               std::vector<int16_t> wav_samples) {
  std::lock_guard<std::mutex> lock(mutex_);
  const StationKey key{band, frequency_hz, hd_subchannel, 0};
  std::shared_ptr<const std::vector<int16_t>> shared_samples;
  if (wav_samples.empty()) {
    station_wav_samples_.erase(key);
  } else {
    shared_samples =
        std::make_shared<const std::vector<int16_t>>(std::move(wav_samples));
    station_wav_samples_[key] = shared_samples;
  }
  if (state_.is_playing && state_.band == band &&
      state_.frequency_hz == frequency_hz &&
      state_.hd_subchannel == hd_subchannel) {
    state_.station_wav_samples = std::move(shared_samples);
  }
}

std::vector<StationProgram> TunerState::GetStations(
    std::optional<RadioBand> band) const {
  std::lock_guard<std::mutex> lock(mutex_);
  std::vector<StationProgram> result;
  for (const auto& [key, station] : stations_) {
    if (band.has_value() && station.band() != *band) {
      continue;
    }
    result.push_back(station);
  }
  return result;
}

std::optional<StationProgram> TunerState::FindStation(
    RadioBand band, uint32_t frequency_hz, uint32_t hd_subchannel) const {
  std::lock_guard<std::mutex> lock(mutex_);
  return FindStationLocked(band, frequency_hz, hd_subchannel);
}

std::optional<StationProgram> TunerState::Seek(RadioBand band,
                                               uint32_t start_frequency_hz,
                                               uint32_t start_hd_subchannel,
                                               bool direction_up,
                                               bool skip_subchannel) const {
  std::vector<StationProgram> candidates = GetStations(band);
  if (candidates.empty()) {
    return std::nullopt;
  }
  if (skip_subchannel) {
    candidates.erase(std::remove_if(candidates.begin(), candidates.end(),
                                    [](const StationProgram& s) {
                                      return s.has_hd_subchannel() &&
                                             s.hd_subchannel() > 0;
                                    }),
                     candidates.end());
    if (candidates.empty()) {
      return std::nullopt;
    }
  }

  auto cmp = [](const StationProgram& a, const StationProgram& b) {
    if (a.frequency_hz() != b.frequency_hz()) {
      return a.frequency_hz() < b.frequency_hz();
    }
    return a.hd_subchannel() < b.hd_subchannel();
  };
  std::sort(candidates.begin(), candidates.end(), cmp);

  if (direction_up) {
    auto it = std::find_if(candidates.begin(), candidates.end(),
                           [&](const StationProgram& s) {
                             if (skip_subchannel) {
                               return s.frequency_hz() > start_frequency_hz;
                             }
                             return (s.frequency_hz() > start_frequency_hz) ||
                                    (s.frequency_hz() == start_frequency_hz &&
                                     s.hd_subchannel() > start_hd_subchannel);
                           });
    return (it != candidates.end()) ? *it : candidates.front();
  }
  auto it = std::find_if(candidates.rbegin(), candidates.rend(),
                         [&](const StationProgram& s) {
                           if (skip_subchannel) {
                             return s.frequency_hz() < start_frequency_hz;
                           }
                           return (s.frequency_hz() < start_frequency_hz) ||
                                  (s.frequency_hz() == start_frequency_hz &&
                                   s.hd_subchannel() < start_hd_subchannel);
                         });
  return (it != candidates.rend()) ? *it : candidates.back();
}

StationProgram TunerState::UpsertStation(StationProgram station) {
  std::lock_guard<std::mutex> lock(mutex_);
  const StationKey key{station.band(), station.frequency_hz(),
                       station.hd_subchannel(), station.dab_sid_ext(),
                       station.dab_ensemble()};
  stations_[key] = station;
  if (station.has_hd_subchannel()) {
    RecomputeSubchannelBitmaskLocked(station.band(), station.frequency_hz());
    station = stations_[key];
  }
  if (state_.is_playing && state_.band == station.band() &&
      state_.frequency_hz == station.frequency_hz() &&
      state_.hd_subchannel == station.hd_subchannel()) {
    current_station_ = station;
    state_.station_present = true;
  }
  return station;
}

bool TunerState::RemoveStation(RadioBand band, uint32_t frequency_hz,
                               std::optional<uint32_t> hd_subchannel,
                               uint64_t dab_sid_ext) {
  std::lock_guard<std::mutex> lock(mutex_);
  bool removed = false;
  for (auto it = stations_.begin(); it != stations_.end();) {
    const StationProgram& s = it->second;
    const bool match_band = (s.band() == band);
    const bool match_freq =
        (frequency_hz == 0 || s.frequency_hz() == frequency_hz);
    const bool match_hd =
        (!hd_subchannel.has_value() || s.hd_subchannel() == *hd_subchannel);
    const bool match_dab = (dab_sid_ext == 0 || s.dab_sid_ext() == dab_sid_ext);
    if (match_band && match_freq && match_hd && match_dab) {
      it = stations_.erase(it);
      removed = true;
    } else {
      ++it;
    }
  }
  if (removed && frequency_hz != 0) {
    RecomputeSubchannelBitmaskLocked(band, frequency_hz);
  }
  if (removed && state_.is_playing) {
    current_station_ = FindStationLocked(state_.band, state_.frequency_hz,
                                         state_.hd_subchannel);
    state_.station_present = current_station_.has_value();
  }
  return removed;
}

std::optional<StationProgram> TunerState::FindStationLocked(
    RadioBand band, uint32_t frequency_hz, uint32_t hd_subchannel) const {
  for (const auto& [key, station] : stations_) {
    if (station.band() == band && station.frequency_hz() == frequency_hz &&
        station.hd_subchannel() == hd_subchannel) {
      return station;
    }
  }
  return std::nullopt;
}

void TunerState::RecomputeSubchannelBitmaskLocked(RadioBand band,
                                                  uint32_t frequency_hz) {
  int32_t mask = 0;
  for (const auto& [key, station] : stations_) {
    if (station.band() == band && station.frequency_hz() == frequency_hz &&
        station.has_hd_subchannel()) {
      mask |= (1 << station.hd_subchannel());
    }
  }
  for (auto& [key, station] : stations_) {
    if (station.band() == band && station.frequency_hz() == frequency_hz &&
        station.has_hd_subchannel()) {
      station.set_hd_subchannels_available(mask);
    }
  }
}

TunerStateSnapshot TunerState::GetSnapshot() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return state_;
}

TunerStatus TunerState::GetStatus() const {
  std::lock_guard<std::mutex> lock(mutex_);
  TunerStatus status;
  status.set_is_playing(state_.is_playing);
  status.set_band(state_.band);
  status.set_frequency_hz(state_.frequency_hz);
  status.set_hd_subchannel(state_.hd_subchannel);
  if (current_station_.has_value()) {
    *status.mutable_current_station() = *current_station_;
  }
  return status;
}

uint32_t TunerState::GetFrequency() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return state_.frequency_hz;
}

RadioBand TunerState::GetBand() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return state_.band;
}

uint32_t TunerState::GetSubchannel() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return state_.hd_subchannel;
}

bool TunerState::IsPlaying() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return state_.is_playing;
}

}  // namespace virtualtuner
}  // namespace cuttlefish
