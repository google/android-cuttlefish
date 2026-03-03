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

#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>
#include <vector>

#include "cuttlefish/host/frontend/webrtc/audio_mixer.h"
#include "cuttlefish/host/frontend/webrtc/audio_settings.h"
#include "cuttlefish/host/frontend/webrtc/libcommon/audio_source.h"
#include "cuttlefish/host/frontend/webrtc/libdevice/audio_sink.h"
#include "cuttlefish/host/libs/audio_connector/server.h"

namespace cuttlefish {

class AudioHandler : public AudioServerExecutor {
  struct StreamDesc {
    std::mutex mtx;
    std::vector<uint8_t> holding_buffer;
    uint32_t sample_rate = 0;
    uint8_t bits_per_sample = 0;
    uint8_t channels = 0;
    bool active = false;

    // Controls
    struct Volume {
      uint32_t min = 0;
      uint32_t max = 1;
      uint32_t current = max;

      float GetCurrentVolumeLevel() {
        return static_cast<float>(current - min) /
               static_cast<float>(max - min);
      };
    };
    Volume volume;
    bool muted = false;
  };

  struct ControlDesc {
    enum class Type {
      Mute,
      Volume,
    };

    Type type = Type::Mute;
    size_t stream_id = 0;
  };

 public:
  AudioHandler(std::unique_ptr<AudioServer> audio_server,
               std::shared_ptr<webrtc_streaming::AudioSink> audio_sink,
               std::shared_ptr<webrtc_streaming::AudioSource> audio_source,
               const std::vector<AudioStreamSettings>& stream_settings,
               const AudioMixerSettings& mixer_settings);
  ~AudioHandler() override;

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
  void ControlsInfo(ControlInfoCommand& cmd) override;
  void OnControlCommand(ControlCommand& cmd) override;

  void OnPlaybackBuffer(TxBuffer buffer) override;
  void OnCaptureBuffer(RxBuffer buffer) override;

 private:
  [[noreturn]] void Loop();
  bool IsCapture(uint32_t stream_id) const;

  std::unique_ptr<AudioServer> audio_server_;
  std::thread server_thread_;
  std::shared_ptr<webrtc_streaming::AudioSource> audio_source_;
  std::vector<virtio_snd_pcm_info> streams_;
  std::vector<StreamDesc> stream_descs_ = {};
  std::vector<virtio_snd_chmap_info> chmaps_;
  std::vector<virtio_snd_ctl_info> controls_;
  std::vector<ControlDesc> controls_to_streams_map_;
  std::unique_ptr<AudioMixer> audio_mixer_;
};
}  // namespace cuttlefish
