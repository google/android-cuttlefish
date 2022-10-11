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

#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#include "host/frontend/webrtc/libdevice/audio_sink.h"
#include "host/frontend/webrtc/libcommon/audio_source.h"
#include "host/libs/audio_connector/server.h"

namespace cuttlefish {
class AudioHandler : public AudioServerExecutor {
  // TODO(jemoreira): This can probably be avoided if playback goes through the
  // audio device instead.
  struct HoldingBuffer {
    std::vector<uint8_t> buffer;
    size_t count;

    void Reset(size_t size);
    size_t Add(const volatile uint8_t* data, size_t max_len);
    size_t Take(uint8_t* dst, size_t len);
    bool empty() const;
    bool full() const;
    size_t freeCapacity() const;
    uint8_t* data();
    uint8_t* end();
  };
  struct StreamDesc {
    std::mutex mtx;
    int bits_per_sample = -1;
    int sample_rate = -1;
    int channels = -1;
    bool active = false;
    HoldingBuffer buffer;
  };

 public:
  AudioHandler(std::unique_ptr<AudioServer> audio_server,
               std::shared_ptr<webrtc_streaming::AudioSink> audio_sink,
               std::shared_ptr<webrtc_streaming::AudioSource> audio_source);
  ~AudioHandler() override = default;

  void Start();

  // AudioServerExecutor implementation
  void StreamsInfo(StreamInfoCommand& cmd) override;
  void SetStreamParameters(StreamSetParamsCommand& cmd) override;
  void PrepareStream(StreamControlCommand& cmd) override;
  void ReleaseStream(StreamControlCommand& cmd) override;
  void StartStream(StreamControlCommand& cmd) override;
  void StopStream(StreamControlCommand& cmd) override;
  void ChmapsInfo(ChmapInfoCommand& cmd) override;
  void JacksInfo(JackInfoCommand& cmd) override;

  void OnPlaybackBuffer(TxBuffer buffer) override;
  void OnCaptureBuffer(RxBuffer buffer) override;

 private:
  [[noreturn]] void Loop();

  std::shared_ptr<webrtc_streaming::AudioSink> audio_sink_;
  std::unique_ptr<AudioServer> audio_server_;
  std::thread server_thread_;
  std::vector<StreamDesc> stream_descs_ = {};
  std::shared_ptr<webrtc_streaming::AudioSource> audio_source_;
};
}  // namespace cuttlefish
