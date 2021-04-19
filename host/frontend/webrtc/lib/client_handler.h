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
#include <vector>

#include <json/json.h>

#include <api/peer_connection_interface.h>
#include <pc/video_track_source.h>

#include "host/frontend/webrtc/lib/connection_observer.h"

namespace cuttlefish {
namespace webrtc_streaming {

class InputChannelHandler;
class AdbChannelHandler;
class ControlChannelHandler;
class BluetoothChannelHandler;

class ClientHandler : public webrtc::PeerConnectionObserver,
                      public std::enable_shared_from_this<ClientHandler> {
 public:
  static std::shared_ptr<ClientHandler> Create(
      int client_id, std::shared_ptr<ConnectionObserver> observer,
      std::function<void(const Json::Value&)> send_client_cb,
      std::function<void()> on_connection_closed_cb);
  ~ClientHandler() override;

  bool SetPeerConnection(
      rtc::scoped_refptr<webrtc::PeerConnectionInterface> peer_connection);

  bool AddDisplay(rtc::scoped_refptr<webrtc::VideoTrackInterface> track,
                  const std::string& label);

  bool AddAudio(rtc::scoped_refptr<webrtc::AudioTrackInterface> track,
                  const std::string& label);

  void HandleMessage(const Json::Value& client_message);

  // CreateSessionDescriptionObserver implementation
  void OnCreateSDPSuccess(webrtc::SessionDescriptionInterface* desc);
  void OnCreateSDPFailure(webrtc::RTCError error);

  // SetSessionDescriptionObserver implementation
  void OnSetSDPFailure(webrtc::RTCError error);

  // PeerConnectionObserver implementation
  void OnSignalingChange(
      webrtc::PeerConnectionInterface::SignalingState new_state) override;
  void OnDataChannel(
      rtc::scoped_refptr<webrtc::DataChannelInterface> data_channel) override;
  void OnRenegotiationNeeded() override;
  void OnStandardizedIceConnectionChange(
      webrtc::PeerConnectionInterface::IceConnectionState new_state) override;
  void OnConnectionChange(
      webrtc::PeerConnectionInterface::PeerConnectionState new_state) override;
  void OnIceGatheringChange(
      webrtc::PeerConnectionInterface::IceGatheringState new_state) override;
  void OnIceCandidate(const webrtc::IceCandidateInterface* candidate) override;
  // Gathering of an ICE candidate failed.
  // See https://w3c.github.io/webrtc-pc/#event-icecandidateerror
  // |host_candidate| is a stringified socket address.
  void OnIceCandidateError(const std::string& host_candidate,
                           const std::string& url, int error_code,
                           const std::string& error_text) override;
  // Gathering of an ICE candidate failed.
  // See https://w3c.github.io/webrtc-pc/#event-icecandidateerror
  void OnIceCandidateError(const std::string& address, int port,
                           const std::string& url, int error_code,
                           const std::string& error_text) override;
  void OnIceCandidatesRemoved(
      const std::vector<cricket::Candidate>& candidates) override;
  void OnTrack(
      rtc::scoped_refptr<webrtc::RtpTransceiverInterface> transceiver) override;
  void OnRemoveTrack(
      rtc::scoped_refptr<webrtc::RtpReceiverInterface> receiver) override;

 private:
  enum class State {
      kNew,
      kCreatingOffer,
      kAwaitingAnswer,
      kConnecting,
      kConnected,
      kFailed,
  };
  ClientHandler(int client_id, std::shared_ptr<ConnectionObserver> observer,
                std::function<void(const Json::Value&)> send_client_cb,
                std::function<void()> on_connection_closed_cb);

  // Intentionally private, disconnect the client by destroying the object.
  void Close();

  void LogAndReplyError(const std::string& error_msg) const;

  int client_id_;
  State state_ = State::kNew;
  std::shared_ptr<ConnectionObserver> observer_;
  std::function<void(const Json::Value&)> send_to_client_;
  std::function<void()> on_connection_closed_cb_;
  rtc::scoped_refptr<webrtc::PeerConnectionInterface> peer_connection_;
  std::vector<rtc::scoped_refptr<webrtc::DataChannelInterface>> data_channels_;
  std::unique_ptr<InputChannelHandler> input_handler_;
  std::unique_ptr<AdbChannelHandler> adb_handler_;
  std::unique_ptr<ControlChannelHandler> control_handler_;
  std::unique_ptr<BluetoothChannelHandler> bluetooth_handler_;
};

}  // namespace webrtc_streaming
}  // namespace cuttlefish
