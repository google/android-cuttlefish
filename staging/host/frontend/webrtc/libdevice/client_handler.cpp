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

#define LOG_TAG "ClientHandler"

#include "host/frontend/webrtc/libdevice/client_handler.h"

#include <netdb.h>
#include <openssl/rand.h>

#include <android-base/logging.h>

#include "host/libs/config/cuttlefish_config.h"

namespace cuttlefish {
namespace webrtc_streaming {

// Video streams initiating in the client may be added and removed at unexpected
// times, causing the webrtc objects to be destroyed and created every time.
// This class hides away that complexity and allows to set up sinks only once.
class ClientVideoTrackImpl : public ClientVideoTrackInterface {
 public:
  void AddOrUpdateSink(rtc::VideoSinkInterface<webrtc::VideoFrame> *sink,
                       const rtc::VideoSinkWants &wants) override {
    sink_ = sink;
    wants_ = wants;
    if (video_track_) {
      video_track_->AddOrUpdateSink(sink, wants);
    }
  }

  void SetVideoTrack(webrtc::VideoTrackInterface *track) {
    video_track_ = track;
    if (sink_) {
      video_track_->AddOrUpdateSink(sink_, wants_);
    }
  }

  void UnsetVideoTrack(webrtc::VideoTrackInterface *track) {
    if (track == video_track_) {
      video_track_ = nullptr;
    }
  }

 private:
  webrtc::VideoTrackInterface* video_track_;
  rtc::VideoSinkInterface<webrtc::VideoFrame> *sink_ = nullptr;
  rtc::VideoSinkWants wants_ = {};
};

std::shared_ptr<ClientHandler> ClientHandler::Create(
    int client_id, std::shared_ptr<ConnectionObserver> observer,
    PeerConnectionBuilder &connection_builder,
    std::function<void(const Json::Value &)> send_to_client_cb,
    std::function<void(bool)> on_connection_changed_cb) {
  return std::shared_ptr<ClientHandler>(
      new ClientHandler(client_id, observer, connection_builder,
                        send_to_client_cb, on_connection_changed_cb));
}

ClientHandler::ClientHandler(
    int client_id, std::shared_ptr<ConnectionObserver> observer,
    PeerConnectionBuilder &connection_builder,
    std::function<void(const Json::Value &)> send_to_client_cb,
    std::function<void(bool)> on_connection_changed_cb)
    : client_id_(client_id),
      observer_(observer),
      send_to_client_(send_to_client_cb),
      on_connection_changed_cb_(on_connection_changed_cb),
      connection_builder_(connection_builder),
      controller_(*this, *this, *this),
      data_channels_handler_(observer),
      camera_track_(new ClientVideoTrackImpl()) {}

rtc::scoped_refptr<webrtc::RtpSenderInterface>
ClientHandler::AddTrackToConnection(
    rtc::scoped_refptr<webrtc::MediaStreamTrackInterface> track,
    rtc::scoped_refptr<webrtc::PeerConnectionInterface> peer_connection,
    const std::string &label) {
  if (!peer_connection) {
    return nullptr;
  }
  // Send each track as part of a different stream with the label as id
  auto err_or_sender =
      peer_connection->AddTrack(track, {label} /* stream_id */);
  if (!err_or_sender.ok()) {
    LOG(ERROR) << "Failed to add track to the peer connection";
    return nullptr;
  }
  return err_or_sender.MoveValue();
}

bool ClientHandler::AddDisplay(
    rtc::scoped_refptr<webrtc::VideoTrackInterface> video_track,
    const std::string &label) {
  auto [it, inserted] = displays_.emplace(label, DisplayTrackAndSender{
                                                     .track = video_track,
                                                 });
  auto sender =
      AddTrackToConnection(video_track, controller_.peer_connection(), label);
  if (sender) {
    DisplayTrackAndSender &info = it->second;
    info.sender = sender;
  }
  // Succeed if the peer connection is null or the track was added
  return controller_.peer_connection() == nullptr || sender;
}

bool ClientHandler::RemoveDisplay(const std::string &label) {
  auto it = displays_.find(label);
  if (it == displays_.end()) {
    return false;
  }

  if (controller_.peer_connection()) {
    DisplayTrackAndSender &info = it->second;

    auto error = controller_.peer_connection()->RemoveTrackOrError(info.sender);
    if (!error.ok()) {
      LOG(ERROR) << "Failed to remove video track for display " << label << ": "
                 << error.message();
      return false;
    }
  }

  displays_.erase(it);
  return true;
}

bool ClientHandler::AddAudio(
    rtc::scoped_refptr<webrtc::AudioTrackInterface> audio_track,
    const std::string &label) {
  audio_streams_.emplace_back(audio_track, label);
  auto peer_connection = controller_.peer_connection();
  if (!peer_connection) {
    return true;
  }
  return AddTrackToConnection(audio_track, controller_.peer_connection(), label)
      .get();
}

ClientVideoTrackInterface* ClientHandler::GetCameraStream() {
  return camera_track_.get();
}

Result<void> ClientHandler::SendMessage(const Json::Value &msg) {
  send_to_client_(msg);
  return {};
}

Result<rtc::scoped_refptr<webrtc::PeerConnectionInterface>>
ClientHandler::Build(
    webrtc::PeerConnectionObserver &observer,
    const std::vector<webrtc::PeerConnectionInterface::IceServer>
        &per_connection_servers) {
  auto peer_connection =
      CF_EXPECT(connection_builder_.Build(observer, per_connection_servers));

  // Re-add the video and audio tracks after the peer connection has been
  // created
  for (auto &[label, info] : displays_) {
    info.sender =
        CF_EXPECT(AddTrackToConnection(info.track, peer_connection, label).get());
  }
  // Add the audio tracks to the peer connection
  for (auto &[audio_track, label] : audio_streams_) {
    // Audio channels are never removed from the connection by the device, so
    // it's ok to discard the returned sender here. The peer connection keeps
    // track of it anyways.
    CF_EXPECT(AddTrackToConnection(audio_track, peer_connection, label).get());
  }

  // libwebrtc configures the video encoder with a start bitrate of just 300kbs
  // which causes it to drop the first 4 frames it receives. Any value over 2Mbs
  // will be capped at 2Mbs when passed to the encoder by the peer_connection
  // object, so we pass the maximum possible value here.
  webrtc::BitrateSettings bitrate_settings;
  bitrate_settings.start_bitrate_bps = 2000000;  // 2Mbs
  peer_connection->SetBitrate(bitrate_settings);

  // At least one data channel needs to be created on the side that creates the
  // SDP offer (the device) for data channels to be enabled at all.
  // This channel is meant to carry control commands from the client.
  auto control_channel = peer_connection->CreateDataChannel(
      kControlChannelLabel, nullptr /* config */);
  CF_EXPECT(control_channel.get(), "Failed to create control data channel");

  data_channels_handler_.OnDataChannelOpen(control_channel);

  return peer_connection;
}

void ClientHandler::HandleMessage(const Json::Value &message) {
  controller_.HandleSignalingMessage(message);
}

void ClientHandler::Close() {
  // We can't simply call peer_connection_->Close() here because this method
  // could be called from one of the PeerConnectionObserver callbacks and that
  // would lead to a deadlock (Close eventually tries to destroy an object that
  // will then wait for the callback to return -> deadlock). Destroying the
  // peer_connection_ has the same effect. The only alternative is to postpone
  // that operation until after the callback returns.
  on_connection_changed_cb_(false);
}

void ClientHandler::OnConnectionStateChange(
    Result<webrtc::PeerConnectionInterface::PeerConnectionState> new_state) {
  if (!new_state.ok()) {
    LOG(ERROR) << "Connection error: " << new_state.error().Message();
    LOG(DEBUG) << new_state.error().Trace();
    Close();
    return;
  }
  switch (*new_state) {
    case webrtc::PeerConnectionInterface::PeerConnectionState::kConnected:
      LOG(VERBOSE) << "Client " << client_id_ << ": WebRTC connected";
      observer_->OnConnected();
      on_connection_changed_cb_(true);
      break;
    case webrtc::PeerConnectionInterface::PeerConnectionState::kDisconnected:
      LOG(VERBOSE) << "Client " << client_id_ << ": Connection disconnected";
      Close();
      break;
    case webrtc::PeerConnectionInterface::PeerConnectionState::kFailed:
      LOG(ERROR) << "Client " << client_id_ << ": Connection failed";
      Close();
      break;
    case webrtc::PeerConnectionInterface::PeerConnectionState::kClosed:
      LOG(VERBOSE) << "Client " << client_id_ << ": Connection closed";
      Close();
      break;
    case webrtc::PeerConnectionInterface::PeerConnectionState::kNew:
      LOG(VERBOSE) << "Client " << client_id_ << ": Connection new";
      break;
    case webrtc::PeerConnectionInterface::PeerConnectionState::kConnecting:
      LOG(VERBOSE) << "Client " << client_id_ << ": Connection started";
      break;
  }
}

void ClientHandler::OnDataChannel(
    rtc::scoped_refptr<webrtc::DataChannelInterface> data_channel) {
  data_channels_handler_.OnDataChannelOpen(data_channel);
}

void ClientHandler::OnTrack(
    rtc::scoped_refptr<webrtc::RtpTransceiverInterface> transceiver) {
  auto track = transceiver->receiver()->track();
  if (track && track->kind() == webrtc::MediaStreamTrackInterface::kVideoKind) {
    // It's ok to take the raw pointer here because we make sure to unset it
    // when the track is removed
    camera_track_->SetVideoTrack(
        static_cast<webrtc::VideoTrackInterface *>(track.get()));
  }
}
void ClientHandler::OnRemoveTrack(
    rtc::scoped_refptr<webrtc::RtpReceiverInterface> receiver) {
  auto track = receiver->track();
  if (track && track->kind() == webrtc::MediaStreamTrackInterface::kVideoKind) {
    // this only unsets if the track matches the one already in store
    camera_track_->UnsetVideoTrack(
        reinterpret_cast<webrtc::VideoTrackInterface *>(track.get()));
  }
}

}  // namespace webrtc_streaming
}  // namespace cuttlefish
