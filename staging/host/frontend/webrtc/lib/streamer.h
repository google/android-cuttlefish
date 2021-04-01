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

#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "host/libs/config/custom_actions.h"

#include "host/frontend/webrtc/lib/audio_sink.h"
#include "host/frontend/webrtc/lib/audio_source.h"
#include "host/frontend/webrtc/lib/connection_observer.h"
#include "host/frontend/webrtc/lib/local_recorder.h"
#include "host/frontend/webrtc/lib/video_sink.h"
#include "host/frontend/webrtc/lib/ws_connection.h"

namespace cuttlefish {
namespace webrtc_streaming {

class ClientHandler;

struct StreamerConfig {
  // The id with which to register with the operator server.
  std::string device_id;
  struct {
    // The ip address or domain name of the operator server.
    std::string addr;
    int port;
    // The path component of the operator server's register url.
    std::string path;
    // The security level to use when connecting to the operator server.
    WsConnection::Security security;
    // A list of key value pairs to include as HTTP handshake headers when
    // connecting to the operator.
    std::vector<std::pair<std::string, std::string>> http_headers;
  } operator_server;
  // The port ranges webrtc is allowed to use.
  // [0,0] means all ports
  std::pair<uint16_t, uint16_t> udp_port_range = {15550, 15558};
  std::pair<uint16_t, uint16_t> tcp_port_range = {15550, 15558};
};

class OperatorObserver {
 public:
  virtual ~OperatorObserver() = default;
  // Called when the websocket connection with the operator is established.
  virtual void OnRegistered() = 0;
  // Called when the websocket connection with the operator is closed.
  virtual void OnClose() = 0;
  // Called when an error is encountered in the connection to the operator.
  virtual void OnError() = 0;
};

class Streamer {
 public:
  // The observer_factory will be used to create an observer for every new
  // client connection. Unregister() needs to be called to stop accepting
  // connections.
  static std::unique_ptr<Streamer> Create(
      const StreamerConfig& cfg,
      std::shared_ptr<ConnectionObserverFactory> factory);
  ~Streamer() = default;

  std::shared_ptr<VideoSink> AddDisplay(const std::string& label, int width,
                                        int height, int dpi,
                                        bool touch_enabled);

  void SetHardwareSpec(std::string key, std::string value);

  template <typename V>
  void SetHardwareSpec(std::string key, V value) {
    SetHardwareSpec(key, std::to_string(value));
  }

  std::shared_ptr<AudioSink> AddAudioStream(const std::string& label);
  // Grants access to streams originating on the client side. If there are
  // multiple streams (either because one client sends more than one or there
  // are several clients) the audio will be mixed and provided as a single
  // stream here.
  std::shared_ptr<AudioSource> GetAudioSource();

  // Add a custom button to the control panel.
  void AddCustomControlPanelButton(const std::string& command,
                                   const std::string& title,
                                   const std::string& icon_name);
  void AddCustomControlPanelButtonWithShellCommand(
      const std::string& command, const std::string& title,
      const std::string& icon_name, const std::string& shell_command);
  void AddCustomControlPanelButtonWithDeviceStates(
      const std::string& command, const std::string& title,
      const std::string& icon_name,
      const std::vector<DeviceState>& device_states);

  // Register with the operator.
  void Register(std::weak_ptr<OperatorObserver> operator_observer);
  void Unregister();

  void RecordDisplays(LocalRecorder& recorder);
 private:
  /*
   * Private Implementation idiom.
   * https://en.cppreference.com/w/cpp/language/pimpl
   */
  class Impl;

  Streamer(std::unique_ptr<Impl> impl);
  std::shared_ptr<Impl> impl_;
};

}  // namespace webrtc_streaming
}  // namespace cuttlefish
