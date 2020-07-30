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
#include <string>
#include <utility>
#include <vector>

#include <api/peer_connection_interface.h>
#include <json/json.h>
#include <pc/video_track_source.h>

#include "host/frontend/webrtc/lib/connection_observer.h"
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
  } operator_server;
  // The port ranges webrtc is allowed to use.
  // [0,0] means all ports
  std::pair<uint16_t, uint16_t> udp_port_range = {15550, 15558};
  std::pair<uint16_t, uint16_t> tcp_port_range = {15550, 15558};
};

class OperatorObserver {
 public:
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
  static std::shared_ptr<Streamer> Create(
      const StreamerConfig& cfg,
      std::shared_ptr<ConnectionObserverFactory> factory);
  ~Streamer() = default;

  std::shared_ptr<VideoSink> AddDisplay(
      const std::string& label, int width, int height, int dpi,
      bool touch_enabled);

  // TODO (b/128328845): Implement audio
  std::shared_ptr<webrtc::AudioSinkInterface> AddAudio(
      const std::string& label);

  // Register with the operator.
  void Register(std::weak_ptr<OperatorObserver> operator_observer);
  void Unregister();

 private:
  // This allows the websocket observer methods to be private in Streamer.
  class WsObserver : public WsConnectionObserver {
   public:
    WsObserver(Streamer* streamer) : streamer_(streamer) {}
    ~WsObserver() override = default;

    void OnOpen() override { streamer_->OnOpen(); }
    void OnClose() override { streamer_->OnClose(); }
    void OnError(const std::string& error) override {
      streamer_->OnError(error);
    }
    void OnReceive(const uint8_t* msg, size_t length, bool is_binary) override {
      streamer_->OnReceive(msg, length, is_binary);
    }

   private:
    Streamer* streamer_;
  };
  struct DisplayDescriptor {
    int width;
    int height;
    int dpi;
    bool touch_enabled;
    rtc::scoped_refptr<webrtc::VideoTrackSourceInterface> source;
  };
  // TODO (jemoreira): move to a place in common with the signaling server
  struct OperatorServerConfig {
    std::vector<webrtc::PeerConnectionInterface::IceServer> servers;
  };

  Streamer(const StreamerConfig& cfg,
           rtc::scoped_refptr<webrtc::PeerConnectionFactoryInterface>
               peer_connection_factory,
           std::unique_ptr<rtc::Thread> network_thread,
           std::unique_ptr<rtc::Thread> worker_thread,
           std::unique_ptr<rtc::Thread> signal_thread,
           std::shared_ptr<ConnectionObserverFactory> factory);

  std::shared_ptr<ClientHandler> CreateClientHandler(int client_id);

  void SendMessageToClient(int client_id, const Json::Value& msg);
  void DestroyClientHandler(int client_id);

  // For use by WsObserver
  void OnOpen();
  void OnClose();
  void OnError(const std::string& error);
  void OnReceive(const uint8_t* msg, size_t length, bool is_binary);

  void HandleConfigMessage(const Json::Value& msg);
  void HandleClientMessage(const Json::Value& server_message);

  // All accesses to these variables happen from the signal_thread_, so there is
  // no need for extra synchronization mechanisms (mutex)
  StreamerConfig config_;
  OperatorServerConfig operator_config_;
  std::shared_ptr<WsConnection> server_connection_;
  std::shared_ptr<ConnectionObserverFactory> connection_observer_factory_;
  rtc::scoped_refptr<webrtc::PeerConnectionFactoryInterface>
      peer_connection_factory_;
  std::unique_ptr<rtc::Thread> network_thread_;
  std::unique_ptr<rtc::Thread> worker_thread_;
  std::unique_ptr<rtc::Thread> signal_thread_;
  std::map<std::string, DisplayDescriptor> displays_;
  std::map<int, std::shared_ptr<ClientHandler>> clients_;
  std::shared_ptr<WsObserver> ws_observer_;
  std::weak_ptr<OperatorObserver> operator_observer_;
};

}  // namespace webrtc_streaming
}  // namespace cuttlefish
