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

#pragma once

#include <mutex>
#include <set>

#include <api/media_stream_interface.h>

#include "host/frontend/webrtc/libdevice/audio_sink.h"

namespace cuttlefish {
namespace webrtc_streaming {

class AudioTrackSourceImpl : public webrtc::AudioSourceInterface {
 public:
  AudioTrackSourceImpl() = default;

  // Sets the volume of the source. |volume| is in  the range of [0, 10].
  void SetVolume(double volume) override;

  void RegisterAudioObserver(AudioObserver* observer) override;
  void UnregisterAudioObserver(AudioObserver* observer) override;

  void AddSink(webrtc::AudioTrackSinkInterface* sink) override;
  void RemoveSink(webrtc::AudioTrackSinkInterface* sink) override;

  // Returns options for the AudioSource.
  // (for some of the settings this approach is broken, e.g. setting
  // audio network adaptation on the source is the wrong layer of abstraction).
  virtual const cricket::AudioOptions options() const;

  void OnFrame(std::shared_ptr<AudioFrameBuffer> frame, int64_t timestamp_ms);

  // MediaSourceInterface implementation
  SourceState state() const override;
  bool remote() const override;

  // NotifierInterface implementation
  void RegisterObserver(webrtc::ObserverInterface* observer) override;
  void UnregisterObserver(webrtc::ObserverInterface* observer) override;

 private:
  std::set<AudioObserver*> audio_observers_;
  std::mutex observers_mutex_;
  std::set<webrtc::AudioTrackSinkInterface*> sinks_;
  std::mutex sinks_mutex_;
};

// Wraps an AudioTrackSourceImpl as an implementation of the AudioSink
// interface. This is needed as the AudioTrackSourceImpl is a reference counted
// object that should only be referenced by rtc::scoped_refptr pointers, but the
// AudioSink interface is not a reference counted object and therefore not
// compatible with that kind of pointers. This class can be referenced by a
// shared pointer and it in turn holds a scoped_refptr to the wrapped object.
class AudioTrackSourceImplSinkWrapper : public AudioSink {
 public:
  virtual ~AudioTrackSourceImplSinkWrapper() = default;

  AudioTrackSourceImplSinkWrapper(rtc::scoped_refptr<AudioTrackSourceImpl> obj)
      : track_source_impl_(obj) {}

  void OnFrame(std::shared_ptr<AudioFrameBuffer> frame,
               int64_t timestamp_ms) override {
    track_source_impl_->OnFrame(frame, timestamp_ms);
  }

 private:
  rtc::scoped_refptr<AudioTrackSourceImpl> track_source_impl_;
};

}  // namespace webrtc_streaming
}  // namespace cuttlefish
