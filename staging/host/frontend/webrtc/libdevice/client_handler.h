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
#include <optional>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include <json/json.h>

#include <api/peer_connection_interface.h>
#include <pc/video_track_source.h>

#include "common/libs/utils/result.h"
#include "host/frontend/webrtc/libcommon/connection_controller.h"
#include "host/frontend/webrtc/libdevice/data_channels.h"
#include "host/frontend/webrtc/libdevice/connection_observer.h"

namespace cuttlefish {
namespace webrtc_streaming {

class InputChannelHandler;
class AdbChannelHandler;
class ControlChannelHandler;
class BluetoothChannelHandler;
class CameraChannelHandler;
class LocationChannelHandler;
class KmlLocationsChannelHandler;
class GpxLocationsChannelHandler;

class ClientVideoTrackInterface;
class ClientVideoTrackImpl;
class PeerConnectionBuilder;

class ClientHandler : public ConnectionController::Observer,
                      public PeerConnectionBuilder,
                      public PeerSignalingHandler {
 public:
  static std::shared_ptr<ClientHandler> Create(
      int client_id, std::shared_ptr<ConnectionObserver> observer,
      PeerConnectionBuilder& connection_builder,
      std::function<void(const Json::Value&)> send_client_cb,
      std::function<void(bool)> on_connection_changed_cb);
  ~ClientHandler() override = default;

  bool AddDisplay(rtc::scoped_refptr<webrtc::VideoTrackInterface> track,
                  const std::string& label);
  bool RemoveDisplay(const std::string& label);

  bool AddAudio(rtc::scoped_refptr<webrtc::AudioTrackInterface> track,
                const std::string& label);

  ClientVideoTrackInterface* GetCameraStream();

  void HandleMessage(const Json::Value& client_message);

  // ConnectionController::Observer implementation
  void OnConnectionStateChange(
      Result<webrtc::PeerConnectionInterface::PeerConnectionState> status) override;
  void OnDataChannel(
      rtc::scoped_refptr<webrtc::DataChannelInterface> data_channel) override;
  void OnTrack(
      rtc::scoped_refptr<webrtc::RtpTransceiverInterface> transceiver) override;
  void OnRemoveTrack(
      rtc::scoped_refptr<webrtc::RtpReceiverInterface> receiver) override;

  // PeerSignalingHandling implementation
  Result<void> SendMessage(const Json::Value& msg) override;

  // PeerConnectionBuilder implementation
  // Delegates on its own pc builder to create the pc and then adds the displays
  // and other streams as required.
  Result<rtc::scoped_refptr<webrtc::PeerConnectionInterface>> Build(
      webrtc::PeerConnectionObserver& observer,
      const std::vector<webrtc::PeerConnectionInterface::IceServer>&
          per_connection_servers) override;

 private:
  ClientHandler(int client_id, std::shared_ptr<ConnectionObserver> observer,
                PeerConnectionBuilder& connection_builder,
                std::function<void(const Json::Value&)> send_client_cb,
                std::function<void(bool)> on_connection_changed_cb);

  // Intentionally private, disconnect the client by destroying the object.
  void Close();

  void LogAndReplyError(const std::string& error_msg) const;
  Result<void> CreateOffer();
  rtc::scoped_refptr<webrtc::RtpSenderInterface> AddTrackToConnection(
      rtc::scoped_refptr<webrtc::MediaStreamTrackInterface> track,
      rtc::scoped_refptr<webrtc::PeerConnectionInterface> peer_connection,
      const std::string& label);

  int client_id_;
  std::shared_ptr<ConnectionObserver> observer_;
  std::function<void(const Json::Value&)> send_to_client_;
  std::function<void(bool)> on_connection_changed_cb_;
  PeerConnectionBuilder& connection_builder_;
  ConnectionController controller_;
  DataChannelHandlers data_channels_handler_;
  std::unique_ptr<ClientVideoTrackImpl> camera_track_;
  struct DisplayTrackAndSender {
    rtc::scoped_refptr<webrtc::VideoTrackInterface> track;
    rtc::scoped_refptr<webrtc::RtpSenderInterface> sender;
  };
  std::map<std::string, DisplayTrackAndSender> displays_;
  std::vector<
      std::pair<rtc::scoped_refptr<webrtc::AudioTrackInterface>, std::string>>
      audio_streams_;
};

class ClientVideoTrackInterface {
 public:
  virtual ~ClientVideoTrackInterface() = default;
  virtual void AddOrUpdateSink(
      rtc::VideoSinkInterface<webrtc::VideoFrame>* sink,
      const rtc::VideoSinkWants& wants) = 0;
};

}  // namespace webrtc_streaming
}  // namespace cuttlefish
