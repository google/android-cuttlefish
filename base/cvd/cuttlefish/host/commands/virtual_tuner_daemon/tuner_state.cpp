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

#include <cstdint>
#include <mutex>

#include "cuttlefish/host/commands/virtual_tuner_daemon/VirtualTuner.pb.h"

namespace cuttlefish {
namespace virtualtuner {

void TunerState::SetTune(RadioBand band, uint32_t frequency_hz,
                         uint32_t hd_subchannel) {
  std::lock_guard<std::mutex> lock(mutex_);
  state_.band = band;
  state_.frequency_hz = frequency_hz;
  state_.hd_subchannel = hd_subchannel;
  state_.is_playing = true;
}

void TunerState::Stop() {
  std::lock_guard<std::mutex> lock(mutex_);
  state_.frequency_hz = 0;
  state_.is_playing = false;
}

TunerStateSnapshot TunerState::GetSnapshot() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return state_;
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
