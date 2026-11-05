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

#pragma once

#include <compare>
#include <cstdint>
#include <map>
#include <mutex>
#include <optional>
#include <vector>

#include "cuttlefish/host/commands/virtual_tuner_daemon/VirtualTuner.pb.h"

namespace cuttlefish {
namespace virtualtuner {

struct TunerStateSnapshot {
  RadioBand band = RadioBand::FM;
  uint32_t frequency_hz = 0;
  uint32_t hd_subchannel = 0;
  bool is_playing = false;
  bool station_present = false;
};

class TunerState {
 public:
  TunerState();

  void SetTune(RadioBand band, uint32_t frequency_hz, uint32_t hd_subchannel);
  void Stop();

  std::vector<StationProgram> GetStations(std::optional<RadioBand> band) const;
  std::optional<StationProgram> FindStation(RadioBand band,
                                            uint32_t frequency_hz,
                                            uint32_t hd_subchannel) const;
  std::optional<StationProgram> Seek(RadioBand band,
                                     uint32_t start_frequency_hz,
                                     uint32_t start_hd_subchannel,
                                     bool direction_up,
                                     bool skip_subchannel) const;
  StationProgram UpsertStation(StationProgram station);
  bool RemoveStation(RadioBand band, uint32_t frequency_hz,
                     std::optional<uint32_t> hd_subchannel,
                     uint64_t dab_sid_ext);

  TunerStateSnapshot GetSnapshot() const;
  TunerStatus GetStatus() const;
  uint32_t GetFrequency() const;
  RadioBand GetBand() const;
  uint32_t GetSubchannel() const;
  bool IsPlaying() const;

 private:
  struct StationKey {
    RadioBand band = RadioBand::FM;
    uint32_t frequency_hz = 0;
    uint32_t hd_subchannel = 0;
    uint64_t dab_sid_ext = 0;
    uint32_t dab_ensemble = 0;

    auto operator<=>(const StationKey&) const = default;
  };

  std::optional<StationProgram> FindStationLocked(RadioBand band,
                                                  uint32_t frequency_hz,
                                                  uint32_t hd_subchannel) const;
  void RecomputeSubchannelBitmaskLocked(RadioBand band, uint32_t frequency_hz);

  mutable std::mutex mutex_;
  TunerStateSnapshot state_;
  std::optional<StationProgram> current_station_;
  std::map<StationKey, StationProgram> stations_;
};

}  // namespace virtualtuner
}  // namespace cuttlefish
