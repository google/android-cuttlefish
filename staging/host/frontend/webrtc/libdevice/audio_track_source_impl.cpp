/*
 * Copyright (C) 2020 The Android Open Source Project
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

#include "host/frontend/webrtc/libdevice/audio_track_source_impl.h"

#include <android-base/logging.h>

namespace cuttlefish {
namespace webrtc_streaming {

void AudioTrackSourceImpl::SetVolume(double volume) {
  std::lock_guard<std::mutex> lock(observers_mutex_);
  for (auto observer : audio_observers_) {
    observer->OnSetVolume(volume);
  }
}

void AudioTrackSourceImpl::RegisterAudioObserver(AudioObserver* observer) {
  std::lock_guard<std::mutex> lock(observers_mutex_);
  audio_observers_.insert(observer);
}
void AudioTrackSourceImpl::UnregisterAudioObserver(AudioObserver* observer) {
  std::lock_guard<std::mutex> lock(observers_mutex_);
  audio_observers_.erase(observer);
}

void AudioTrackSourceImpl::AddSink(webrtc::AudioTrackSinkInterface* sink) {
  std::lock_guard<std::mutex> lock(sinks_mutex_);
  sinks_.insert(sink);
}

void AudioTrackSourceImpl::RemoveSink(webrtc::AudioTrackSinkInterface* sink) {
  std::lock_guard<std::mutex> lock(sinks_mutex_);
  sinks_.erase(sink);
}

const cricket::AudioOptions AudioTrackSourceImpl::options() const {
  return cricket::AudioOptions();
}

void AudioTrackSourceImpl::OnFrame(std::shared_ptr<AudioFrameBuffer> frame,
                                   int64_t timestamp_ms) {
    std::lock_guard<std::mutex> lock(sinks_mutex_);
    for (auto sink : sinks_) {
      sink->OnData(frame->data(), frame->bits_per_sample(),
                   frame->sample_rate(), frame->channels(), frame->frames(),
                   timestamp_ms);
    }
}

AudioTrackSourceImpl::SourceState AudioTrackSourceImpl::state() const {
  return SourceState::kLive;
}

bool AudioTrackSourceImpl::remote() const { return false; }

void AudioTrackSourceImpl::RegisterObserver(
    webrtc::ObserverInterface* /*observer*/) {}

void AudioTrackSourceImpl::UnregisterObserver(
    webrtc::ObserverInterface* /*observer*/) {}

}  // namespace webrtc_streaming
}  // namespace cuttlefish
