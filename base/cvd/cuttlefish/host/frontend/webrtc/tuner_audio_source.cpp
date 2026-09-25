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

#include "cuttlefish/host/frontend/webrtc/tuner_audio_source.h"

#include <sys/socket.h>  // IWYU pragma: keep
#include <sys/types.h>

#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "absl/log/log.h"

#include "cuttlefish/common/libs/fs/shared_fd.h"

namespace cuttlefish {
namespace {

// Format produced by the virtual tuner daemon (channel count comes from the
// guest audio config).
constexpr int kSampleRate = 48000;
constexpr int kBytesPerSample = sizeof(int16_t);
constexpr size_t kFramesPer10Ms = kSampleRate / 100;

// Two 10 ms periods of headroom before serving, enough to absorb host
// scheduling jitter without adding meaningful latency.
constexpr size_t kPrimePeriods = 2;

// Hard ceiling on buffered audio. Anything beyond this is stale by definition,
// so the oldest frames are dropped rather than played late.
constexpr size_t kMaxFifoPeriods = 10;  // 100 ms

constexpr size_t kReadChunkBytes = 4096;

}  // namespace

TunerAudioSource::TunerAudioSource(const std::string& pcm_socket_path,
                                   uint8_t channels)
    : channels_(channels),
      frame_bytes_(channels_ * kBytesPerSample),
      prime_bytes_(kPrimePeriods * kFramesPer10Ms * frame_bytes_),
      max_fifo_bytes_(kMaxFifoPeriods * kFramesPer10Ms * frame_bytes_),
      fd_(SharedFD::SocketLocalClient(pcm_socket_path, false,
                                      SOCK_STREAM | SOCK_NONBLOCK)) {
  if (fd_->IsOpen()) {
    LOG(INFO) << "Tuner audio source connected to " << pcm_socket_path;
  } else {
    LOG(ERROR) << "Tuner audio source failed to connect to " << pcm_socket_path
               << ": " << fd_->StrError() << ". Emitting silence.";
  }
  fifo_.reserve(max_fifo_bytes_);
}

int TunerAudioSource::GetMoreAudioData(void* data, int bytes_per_sample,
                                       int samples_per_channel,
                                       int num_channels, int sample_rate,
                                       bool& muted) {
  muted = false;
  const size_t bytes_needed = static_cast<size_t>(samples_per_channel) *
                              num_channels * bytes_per_sample;

  DrainSocket();
  if (reset_requested_.exchange(false)) {
    // Everything received so far was produced while the guest stream was
    // stopped.
    fifo_.clear();
    primed_ = false;
  }
  TrimFifo();

  if (bytes_per_sample != kBytesPerSample || num_channels != channels_ ||
      sample_rate != kSampleRate) {
    LOG_FIRST_N(WARNING, 1)
        << "Unsupported tuner capture format: " << sample_rate << " Hz, "
        << num_channels << " ch, " << bytes_per_sample * 8 << " bit";
    // Keep draining the socket, but drop the PCM.
    fifo_.clear();
    primed_ = false;
    std::memset(data, 0, bytes_needed);
    return samples_per_channel;
  }

  if (!primed_) {
    if (fifo_.size() < prime_bytes_) {
      std::memset(data, 0, bytes_needed);
      return samples_per_channel;
    }
    primed_ = true;
  }

  if (fifo_.size() < bytes_needed) {
    // Underrun: the daemon is idle or fell behind. Emit silence and re-prime so
    // a single late delivery doesn't cause repeated stuttering.
    primed_ = false;
    ++underrun_count_;
    LogStats();
    std::memset(data, 0, bytes_needed);
    return samples_per_channel;
  }

  std::memcpy(data, fifo_.data(), bytes_needed);
  fifo_.erase(fifo_.begin(), fifo_.begin() + bytes_needed);
  return samples_per_channel;
}

void TunerAudioSource::Reset() { reset_requested_.store(true); }

void TunerAudioSource::DrainSocket() {
  if (!fd_->IsOpen()) {
    return;
  }
  uint8_t chunk[kReadChunkBytes];
  while (true) {
    const ssize_t bytes_read = fd_->Recv(chunk, sizeof(chunk), MSG_DONTWAIT);
    if (bytes_read > 0) {
      fifo_.insert(fifo_.end(), chunk, chunk + bytes_read);
      continue;
    }
    if (bytes_read < 0) {
      const int err = fd_->GetErrno();
      if (err == EAGAIN || err == EWOULDBLOCK) {
        return;
      }
      LOG(ERROR) << "Tuner audio source read failed: " << fd_->StrError()
                 << ". Emitting silence.";
    } else {
      LOG(WARNING) << "Tuner audio source disconnected. Emitting silence.";
    }
    fd_->Close();
    fifo_.clear();
    primed_ = false;
    return;
  }
}

void TunerAudioSource::TrimFifo() {
  if (fifo_.size() <= max_fifo_bytes_) {
    return;
  }
  // Drop whole frames only, so the head of the FIFO stays aligned even if the
  // tail holds a partially received frame.
  size_t excess = fifo_.size() - max_fifo_bytes_;
  excess += (frame_bytes_ - excess % frame_bytes_) % frame_bytes_;
  fifo_.erase(fifo_.begin(), fifo_.begin() + excess);
  ++overflow_count_;
  LogStats();
}

void TunerAudioSource::LogStats() const {
  LOG_EVERY_N_SEC(INFO, 10)
      << "Tuner audio: " << overflow_count_ << " overflows, " << underrun_count_
      << " underruns";
}

}  // namespace cuttlefish
