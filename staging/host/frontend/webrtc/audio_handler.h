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
#include <thread>
#include <vector>

#include "host/frontend/webrtc/lib/audio_sink.h"
#include "host/libs/audio_connector/server.h"

namespace cuttlefish {
class AudioHandler : public AudioServerExecutor {
  struct StreamParameters {
    int bits_per_sample = -1;
    int sample_rate = -1;
    int channels = -1;
    bool active = false;
    bool capture = false;
  };
  struct HoldingBuffer {
    std::vector<uint8_t> buffer;
    size_t count;

    void Reset(size_t size);
    size_t Add(const volatile uint8_t* data, size_t max_len);
    bool empty() const;
    bool full() const;
    uint8_t* data();
  };

 public:
  AudioHandler(std::shared_ptr<webrtc_streaming::AudioSink> audio_sink,
               std::unique_ptr<AudioServer> audio_server);
  ~AudioHandler() override = default;

  void Start();

  // AudioServerExecutor implementation
  void StreamsInfo(StreamInfoCommand& cmd) override;
  void SetStreamParameters(StreamSetParamsCommand& cmd) override;
  void PrepareStream(StreamControlCommand& cmd) override;
  void ReleaseStream(StreamControlCommand& cmd) override;
  void StartStream(StreamControlCommand& cmd) override;
  void StopStream(StreamControlCommand& cmd) override;

  void OnBuffer(TxBuffer buffer) override;

 private:
  [[noreturn]] void Loop();

  std::shared_ptr<webrtc_streaming::AudioSink> audio_sink_;
  std::unique_ptr<AudioServer> audio_server_;
  std::thread server_thread_;
  std::vector<StreamParameters> stream_parameters_;
  std::vector<HoldingBuffer> stream_buffers_;
};
}  // namespace cuttlefish
