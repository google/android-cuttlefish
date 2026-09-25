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

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "cuttlefish/common/libs/fs/shared_fd.h"
#include "cuttlefish/host/frontend/webrtc/libcommon/audio_source.h"

namespace cuttlefish {

// Feeds a dedicated virtio-snd capture stream from the virtual tuner daemon's
// PCM socket. The socket is connected once at construction; if that fails or
// the daemon goes away, the source emits silence from then on, so the guest
// always sees a continuous stream at the correct rate.
//
// The daemon produces 48 kHz, 16-bit PCM with `channels` interleaved channels,
// which must match the channel layout of the tuner stream in the guest audio
// config. The guest negotiates the stream format like for any other stream; if
// it picks a different one, the source logs a warning and emits silence.
//
// GetMoreAudioData() calls must not overlap, though they may come from
// different threads over time. Reset() is safe to call from any thread.
class TunerAudioSource : public webrtc_streaming::AudioSource {
 public:
  TunerAudioSource(const std::string& pcm_socket_path, uint8_t channels);
  ~TunerAudioSource() override = default;

  TunerAudioSource(const TunerAudioSource&) = delete;
  TunerAudioSource& operator=(const TunerAudioSource&) = delete;

  int GetMoreAudioData(void* data, int bytes_per_sample,
                       int samples_per_channel, int num_channels,
                       int sample_rate, bool& muted) override;

  // Only sets a flag; the next GetMoreAudioData() call drops everything
  // received up to that point and re-primes.
  void Reset() override;

 private:
  void DrainSocket();
  void TrimFifo();
  void LogStats() const;

  const int channels_;
  const size_t frame_bytes_;
  const size_t prime_bytes_;
  const size_t max_fifo_bytes_;
  SharedFD fd_;
  std::vector<uint8_t> fifo_;
  bool primed_ = false;
  std::atomic<bool> reset_requested_{false};
  // Drift indicators: overflows mean the daemon runs faster than the guest
  // consumes, underruns mean it runs slower (or stopped sending).
  uint64_t overflow_count_ = 0;
  uint64_t underrun_count_ = 0;
};

}  // namespace cuttlefish
