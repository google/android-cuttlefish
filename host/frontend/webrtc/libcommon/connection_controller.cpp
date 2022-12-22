/*
 * Copyright (C) 2022 The Android Open Source Project
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

#include "host/frontend/webrtc/libcommon/connection_controller.h"

#include <algorithm>
#include <vector>

#include <android-base/logging.h>

#include "host/frontend/webrtc/libcommon/audio_device.h"
#include "host/frontend/webrtc/libcommon/utils.h"

namespace cuttlefish {
namespace webrtc_streaming {

// Different classes are needed because all the interfaces inherit from
// classes providing the methods AddRef and Release, needed by scoped_ptr, which
// cause ambiguity when a single class (i.e ConnectionController) implements all
// of them.
// It's safe for these classes to hold a reference to the ConnectionController
// because it owns the peer connection, so it will never be destroyed before
// these observers.
class CreateSessionDescriptionObserverIntermediate
    : public webrtc::CreateSessionDescriptionObserver {
 public:
  CreateSessionDescriptionObserverIntermediate(ConnectionController& controller)
      : controller_(controller) {}

  void OnSuccess(webrtc::SessionDescriptionInterface* desc) override {
    controller_.OnCreateSDPSuccess(desc);
  }
  void OnFailure(webrtc::RTCError error) override {
    controller_.OnCreateSDPFailure(error);
  }

 private:
  ConnectionController& controller_;
};

class SetSessionDescriptionObserverIntermediate
    : public webrtc::SetSessionDescriptionObserver {
 public:
  SetSessionDescriptionObserverIntermediate(ConnectionController& controller)
      : controller_(controller) {}

  void OnSuccess() override { controller_.OnSetLocalDescriptionSuccess(); }
  void OnFailure(webrtc::RTCError error) override {
    controller_.OnSetLocalDescriptionFailure(error);
  }

 private:
  ConnectionController& controller_;
};

class SetRemoteDescriptionObserverIntermediate
    : public webrtc::SetRemoteDescriptionObserverInterface {
 public:
  SetRemoteDescriptionObserverIntermediate(ConnectionController& controller)
      : controller_(controller) {}

  void OnSetRemoteDescriptionComplete(webrtc::RTCError error) override {
    controller_.OnSetRemoteDescriptionComplete(error);
  }

 private:
  ConnectionController& controller_;
};

ConnectionController::ConnectionController(
    PeerSignalingHandler& sig_handler,
    PeerConnectionBuilder& connection_builder,
    ConnectionController::Observer& observer)
    : sig_handler_(sig_handler),
      connection_builder_(connection_builder),
      observer_(observer) {}

void ConnectionController::CreateOffer() {
  // No memory leak here because this is a ref counted object and the
  // peer connection immediately wraps it with a scoped_refptr
  peer_connection_->CreateOffer(ThisAsCreateSDPObserver(), {} /*options*/);
}

Result<void> ConnectionController::RequestOffer(
    const std::vector<webrtc::PeerConnectionInterface::IceServer>&
        ice_servers) {
  observer_.OnConnectionStateChange(
      webrtc::PeerConnectionInterface::PeerConnectionState::kNew);
  Json::Value msg;
  msg["type"] = "request-offer";
  if (!ice_servers.empty()) {
    // Only include the ice servers in the message if non empty
    msg["ice_servers"] = GenerateIceServersMessage(ice_servers);
  }
  CF_EXPECT(sig_handler_.SendMessage(msg),
            "Failed to send the request-offer message to the device");
  return {};
}

void ConnectionController::FailConnection(const std::string& message) {
  Json::Value reply;
  reply["type"] = "error";
  reply["error"] = message;
  sig_handler_.SendMessage(reply);
  observer_.OnConnectionStateChange(CF_ERR(message));
}

void ConnectionController::AddPendingIceCandidates() {
  // Add any ice candidates that arrived before the remote description
  for (auto& candidate : pending_ice_candidates_) {
    peer_connection_->AddIceCandidate(
        std::move(candidate), [this](webrtc::RTCError error) {
          if (!error.ok()) {
            FailConnection(ToString(error.type()) + std::string(": ") +
                           error.message());
          }
        });
  }
  pending_ice_candidates_.clear();
}

Result<void> ConnectionController::OnOfferRequestMsg(
    const std::vector<webrtc::PeerConnectionInterface::IceServer>&
        ice_servers) {
  peer_connection_ = CF_EXPECT(connection_builder_.Build(*this, ice_servers),
                               "Failed to create peer connection");
  CreateOffer();
  return {};
}

Result<void> ConnectionController::OnOfferMsg(
    std::unique_ptr<webrtc::SessionDescriptionInterface> offer) {
  peer_connection_->SetRemoteDescription(std::move(offer),
                                         ThisAsSetRemoteSDPObserver());
  return {};
}

Result<void> ConnectionController::OnAnswerMsg(
    std::unique_ptr<webrtc::SessionDescriptionInterface> answer) {
  peer_connection_->SetRemoteDescription(std::move(answer),
                                         ThisAsSetRemoteSDPObserver());
  return {};
}

Result<void> ConnectionController::OnIceCandidateMsg(
    std::unique_ptr<webrtc::IceCandidateInterface> candidate) {
  if (peer_connection_->current_remote_description()) {
    peer_connection_->AddIceCandidate(
        std::move(candidate), [this](webrtc::RTCError error) {
          if (!error.ok()) {
            FailConnection(ToString(error.type()) + std::string(": ") +
                           error.message());
          }
        });
  } else {
    // Store the ice candidate to be added later if it arrives before the
    // remote description. This could happen if the client uses polling
    // instead of websockets because the candidates are generated immediately
    // after the remote (offer) description is set and the events and the ajax
    // calls are asynchronous.
    pending_ice_candidates_.push_back(std::move(candidate));
  }
  return {};
}

Result<void> ConnectionController::OnErrorMsg(const std::string& msg) {
  LOG(ERROR) << "Received error message from peer: " << msg;
  return {};
}

void ConnectionController::OnCreateSDPSuccess(
    webrtc::SessionDescriptionInterface* desc) {
  std::string offer_str;
  desc->ToString(&offer_str);
  std::string sdp_type = desc->type();
  peer_connection_->SetLocalDescription(ThisAsSetSDPObserver(), desc);
  // The peer connection takes ownership of the description so it should not be
  // used after this
  desc = nullptr;

  Json::Value reply;
  reply["type"] = sdp_type;
  reply["sdp"] = offer_str;

  sig_handler_.SendMessage(reply);
}

void ConnectionController::OnCreateSDPFailure(const webrtc::RTCError& error) {
  FailConnection(ToString(error.type()) + std::string(": ") + error.message());
}

void ConnectionController::OnSetLocalDescriptionSuccess() {
  // local description set, nothing else to do
}

void ConnectionController::OnSetLocalDescriptionFailure(
    const webrtc::RTCError& error) {
  LOG(ERROR) << "Error setting local description: Either there is a bug in "
                "libwebrtc or the local description was (incorrectly) modified "
                "after creating it";
  FailConnection(ToString(error.type()) + std::string(": ") + error.message());
}

void ConnectionController::OnSetRemoteDescriptionComplete(
    const webrtc::RTCError& error) {
  if (!error.ok()) {
    // The remote description was rejected, can't connect to device.
    FailConnection(ToString(error.type()) + std::string(": ") + error.message());
    return;
  }
  AddPendingIceCandidates();
  auto remote_desc = peer_connection_->current_remote_description();
  CHECK(remote_desc) << "The remote description was just added successfully in "
                        "this thread, so it can't be nullptr";
  if (remote_desc->GetType() != webrtc::SdpType::kOffer) {
    // Only create and send answer when the remote description is an offer.
    return;
  }
  peer_connection_->CreateAnswer(ThisAsCreateSDPObserver(), {} /*options*/);
}

// No memory leaks with these because the peer_connection immediately wraps
// these pointers with scoped_refptr.
webrtc::CreateSessionDescriptionObserver*
ConnectionController::ThisAsCreateSDPObserver() {
  return new rtc::RefCountedObject<
      CreateSessionDescriptionObserverIntermediate>(*this);
}
webrtc::SetSessionDescriptionObserver*
ConnectionController::ThisAsSetSDPObserver() {
  return new rtc::RefCountedObject<SetSessionDescriptionObserverIntermediate>(
      *this);
}
rtc::scoped_refptr<webrtc::SetRemoteDescriptionObserverInterface>
ConnectionController::ThisAsSetRemoteSDPObserver() {
  return rtc::scoped_refptr<webrtc::SetRemoteDescriptionObserverInterface>(
      new rtc::RefCountedObject<SetRemoteDescriptionObserverIntermediate>(
          *this));
}

void ConnectionController::HandleSignalingMessage(const Json::Value& msg) {
  auto result = HandleSignalingMessageInner(msg);
  if (!result.ok()) {
    LOG(ERROR) << result.error().Message();
    LOG(DEBUG) << result.error().Trace();
    FailConnection(result.error().Message());
  }
}

Result<void> ConnectionController::HandleSignalingMessageInner(
    const Json::Value& message) {
  CF_EXPECT(ValidateJsonObject(message, "",
                               {{"type", Json::ValueType::stringValue}}));
  auto type = message["type"].asString();

  if (type == "request-offer") {
    auto ice_servers = CF_EXPECT(ParseIceServersMessage(message),
                                 "Error parsing ice-servers field");
    return OnOfferRequestMsg(ice_servers);
  } else if (type == "offer") {
    auto remote_desc = CF_EXPECT(
        ParseSessionDescription(type, message, webrtc::SdpType::kOffer));
    return OnOfferMsg(std::move(remote_desc));
  } else if (type == "answer") {
    auto remote_desc = CF_EXPECT(
        ParseSessionDescription(type, message, webrtc::SdpType::kAnswer));
    return OnAnswerMsg(std::move(remote_desc));
  } else if (type == "ice-candidate") {
    auto candidate = CF_EXPECT(ParseIceCandidate(type, message));
    return OnIceCandidateMsg(std::move(candidate));
  } else if (type == "error") {
    return OnErrorMsg(CF_EXPECT(ParseError(type, message)));
  } else {
    return CF_ERR("Unknown client message type: " + type);
  }
}

// Triggered when the SignalingState changed.
void ConnectionController::OnSignalingChange(
    webrtc::PeerConnectionInterface::SignalingState new_state) {
  LOG(VERBOSE) << "Signaling state changed: " << new_state;
}

// Triggered when media is received on a new stream from remote peer.
void ConnectionController::OnAddStream(
    rtc::scoped_refptr<webrtc::MediaStreamInterface> stream) {
  LOG(VERBOSE) << "Stream added: " << stream->id();
}

// Triggered when a remote peer closes a stream.
void ConnectionController::OnRemoveStream(
    rtc::scoped_refptr<webrtc::MediaStreamInterface> stream) {
  LOG(VERBOSE) << "Stream removed: " << stream->id();
}

// Triggered when a remote peer opens a data channel.
void ConnectionController::OnDataChannel(
    rtc::scoped_refptr<webrtc::DataChannelInterface> data_channel) {
  observer_.OnDataChannel(data_channel);
}

// Triggered when renegotiation is needed. For example, an ICE restart
// has begun.
void ConnectionController::OnRenegotiationNeeded() {
  if (!peer_connection_) {
    return;
  }
  CreateOffer();
}

// Called any time the standards-compliant IceConnectionState changes.
void ConnectionController::OnStandardizedIceConnectionChange(
    webrtc::PeerConnectionInterface::IceConnectionState new_state) {
  switch (new_state) {
    case webrtc::PeerConnectionInterface::kIceConnectionNew:
      LOG(DEBUG) << "ICE connection state: New";
      break;
    case webrtc::PeerConnectionInterface::kIceConnectionChecking:
      LOG(DEBUG) << "ICE connection state: Checking";
      break;
    case webrtc::PeerConnectionInterface::kIceConnectionConnected:
      LOG(DEBUG) << "ICE connection state: Connected";
      break;
    case webrtc::PeerConnectionInterface::kIceConnectionCompleted:
      LOG(DEBUG) << "ICE connection state: Completed";
      break;
    case webrtc::PeerConnectionInterface::kIceConnectionFailed:
      LOG(DEBUG) << "ICE connection state: Failed";
      break;
    case webrtc::PeerConnectionInterface::kIceConnectionDisconnected:
      LOG(DEBUG) << "ICE connection state: Disconnected";
      break;
    case webrtc::PeerConnectionInterface::kIceConnectionClosed:
      LOG(DEBUG) << "ICE connection state: Closed";
      break;
    case webrtc::PeerConnectionInterface::kIceConnectionMax:
      LOG(DEBUG) << "ICE connection state: Max";
      break;
    default:
      LOG(DEBUG) << "ICE connection state: " << new_state;
  }
}

// Called any time the PeerConnectionState changes.
void ConnectionController::OnConnectionChange(
    webrtc::PeerConnectionInterface::PeerConnectionState new_state) {
  observer_.OnConnectionStateChange(new_state);
}

// Called any time the IceGatheringState changes.
void ConnectionController::OnIceGatheringChange(
    webrtc::PeerConnectionInterface::IceGatheringState new_state) {
  std::string state_str;
  switch (new_state) {
    case webrtc::PeerConnectionInterface::IceGatheringState::kIceGatheringNew:
      state_str = "NEW";
      break;
    case webrtc::PeerConnectionInterface::IceGatheringState::
        kIceGatheringGathering:
      state_str = "GATHERING";
      break;
    case webrtc::PeerConnectionInterface::IceGatheringState::
        kIceGatheringComplete:
      state_str = "COMPLETE";
      break;
    default:
      state_str = "UNKNOWN";
  }
  LOG(VERBOSE) << "ICE Gathering state set to: " << state_str;
}

// A new ICE candidate has been gathered.
void ConnectionController::OnIceCandidate(
    const webrtc::IceCandidateInterface* candidate) {
  std::string candidate_sdp;
  candidate->ToString(&candidate_sdp);
  auto sdp_mid = candidate->sdp_mid();
  auto line_index = candidate->sdp_mline_index();

  Json::Value reply;
  reply["type"] = "ice-candidate";
  reply["mid"] = sdp_mid;
  reply["mLineIndex"] = static_cast<Json::UInt64>(line_index);
  reply["candidate"] = candidate_sdp;

  sig_handler_.SendMessage(reply);
}

// Gathering of an ICE candidate failed.
// See https://w3c.github.io/webrtc-pc/#event-icecandidateerror
void ConnectionController::OnIceCandidateError(const std::string& address,
                                               int port, const std::string& url,
                                               int error_code,
                                               const std::string& error_text) {
  LOG(VERBOSE) << "Gathering of an ICE candidate (address: " << address
               << ", port: " << port << ", url: " << url
               << ") failed: " << error_text;
}

// Ice candidates have been removed.
void ConnectionController::OnIceCandidatesRemoved(
    const std::vector<cricket::Candidate>&) {
  // ignore
}

// This is called when signaling indicates a transceiver will be receiving
// media from the remote endpoint. This is fired during a call to
// SetRemoteDescription. The receiving track can be accessed by:
// ConnectionController::|transceiver->receiver()->track()| and its
// associated streams by |transceiver->receiver()->streams()|. Note: This will
// only be called if Unified Plan semantics are specified. This behavior is
// specified in section 2.2.8.2.5 of the "Set the RTCSessionDescription"
// algorithm: https://w3c.github.io/webrtc-pc/#set-description
void ConnectionController::OnTrack(
    rtc::scoped_refptr<webrtc::RtpTransceiverInterface> transceiver) {
  observer_.OnTrack(transceiver);
}

// Called when signaling indicates that media will no longer be received on a
// track.
// With Plan B semantics, the given receiver will have been removed from the
// PeerConnection and the track muted.
// With Unified Plan semantics, the receiver will remain but the transceiver
// will have changed direction to either sendonly or inactive.
// https://w3c.github.io/webrtc-pc/#process-remote-track-removal
void ConnectionController::OnRemoveTrack(
    rtc::scoped_refptr<webrtc::RtpReceiverInterface> receiver) {
  observer_.OnRemoveTrack(receiver);
}

}  // namespace webrtc_streaming
}  // namespace cuttlefish

