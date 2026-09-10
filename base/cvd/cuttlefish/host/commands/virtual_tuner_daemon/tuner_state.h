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

#include <cstdint>
#include <mutex>

#include "cuttlefish/host/commands/virtual_tuner_daemon/VirtualTuner.pb.h"

namespace cuttlefish {
namespace virtualtuner {

struct TunerStateSnapshot {
  RadioBand band = RadioBand::FM;
  uint32_t frequency_hz = 0;
  uint32_t hd_subchannel = 0;
  bool is_playing = false;
};

class TunerState {
 public:
  TunerState() = default;

  void SetTune(RadioBand band, uint32_t frequency_hz, uint32_t hd_subchannel);
  void Stop();

  TunerStateSnapshot GetSnapshot() const;
  uint32_t GetFrequency() const;
  RadioBand GetBand() const;
  uint32_t GetSubchannel() const;
  bool IsPlaying() const;

 private:
  mutable std::mutex mutex_;
  TunerStateSnapshot state_;
};

}  // namespace virtualtuner
}  // namespace cuttlefish
