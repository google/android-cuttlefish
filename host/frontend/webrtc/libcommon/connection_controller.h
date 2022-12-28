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

#pragma once

#include <memory>

#include <api/peer_connection_interface.h>
#include <json/json.h>

#include "common/libs/utils/result.h"
#include "host/frontend/webrtc/libcommon/peer_signaling_handler.h"

namespace cuttlefish {
namespace webrtc_streaming {

// Creating the peer connection is different on the client and device, but for
// both the pc needs to be created during the signaling process.
class PeerConnectionBuilder {
 public:
  virtual ~PeerConnectionBuilder() = default;
  virtual Result<rtc::scoped_refptr<webrtc::PeerConnectionInterface>> Build(
      webrtc::PeerConnectionObserver& observer,
      const std::vector<webrtc::PeerConnectionInterface::IceServer>&
          per_connection_servers) = 0;
};

// Encapsulates the signaling protocol, which is mostly the same for client and
// device. Devices will create a connection controller for each new client and
// simply provide implementations of the callbacks in
// ConnectionController::Observer. Clients must additionally call RequestOffer
// to start the signaling process.
class ConnectionController : public webrtc::PeerConnectionObserver {
 public:
  // These callbacks will be called from the signaling thread. Implementations
  // should return as soon as possible, particularly not blocking on IO.
  // Implementations must never destroy the ConnectionController object from
  // inside these callbacks as that would lead to undefined behavior.
  // TODO (b/240578845): This avoids having to create an extra thread per
  // client just to monitor changes in the device side, but opens problems by
  // allowing it to react to state changes on a peer connection callback. The
  // device already has code to avoid these issues, but the ideal solution
  // would be for this callback to be posted to a thread or not to be used at
  // all.
  class Observer {
   public:
    virtual ~Observer() = default;
    virtual void OnConnectionStateChange(
        Result<webrtc::PeerConnectionInterface::PeerConnectionState>
            status) = 0;

    // Called when new media tracks are added to the peer connection.
    virtual void OnTrack(
        rtc::scoped_refptr<webrtc::RtpTransceiverInterface> transceiver) = 0;
    // Called when media tracks are removed from the peer connection.
    virtual void OnRemoveTrack(
        rtc::scoped_refptr<webrtc::RtpReceiverInterface> receiver) = 0;
    // Called when a data channel is added to the peer connection.
    virtual void OnDataChannel(
        rtc::scoped_refptr<webrtc::DataChannelInterface> data_channel) = 0;
  };

  ConnectionController(PeerSignalingHandler& sig_handler,
                       PeerConnectionBuilder& connection_builder,
                       Observer& observer);
  ~ConnectionController() override = default;

  // Sends a request-offer message to the peer to kickstart the signaling
  // process.
  Result<void> RequestOffer(
      const std::vector<webrtc::PeerConnectionInterface::IceServer>&
          ice_servers);

  // This class doesn't poll for signaling messages from the server, instead
  // users must do that themselves and provide them here for the connection
  // controller to process them. As the result of this processing some callbacks
  // may be called or new messages may be sent to the peer, most likely after
  // the function returns, but that's not guaranteed.
  void HandleSignalingMessage(const Json::Value& msg);

  rtc::scoped_refptr<webrtc::PeerConnectionInterface> peer_connection() {
    return peer_connection_;
  }

  // webrtc::PeerConnectionObserver implementation
  void OnSignalingChange(
      webrtc::PeerConnectionInterface::SignalingState new_state) override;
  void OnAddStream(
      rtc::scoped_refptr<webrtc::MediaStreamInterface> stream) override;
  void OnRemoveStream(
      rtc::scoped_refptr<webrtc::MediaStreamInterface> stream) override;
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
  void OnIceCandidateError(const std::string& address, int port,
                           const std::string& url, int error_code,
                           const std::string& error_text) override;
  void OnIceCandidatesRemoved(
      const std::vector<cricket::Candidate>& candidates) override;
  // The following can be overridden but are not needed by either the device or
  // client at the moement. void OnIceConnectionReceivingChange(bool receiving)
  // override; void OnIceSelectedCandidatePairChanged( const
  // cricket::CandidatePairChangeEvent& event) override; void OnAddTrack(
  // rtc::scoped_refptr<webrtc::RtpReceiverInterface> receiver,
  // const std::vector<rtc::scoped_refptr<webrtc::MediaStreamInterface>>&
  // streams) override;
  // void OnInterestingUsage(int usage_pattern) override;
  void OnTrack(
      rtc::scoped_refptr<webrtc::RtpTransceiverInterface> transceiver) override;
  void OnRemoveTrack(
      rtc::scoped_refptr<webrtc::RtpReceiverInterface> receiver) override;

 private:
  friend class CreateSessionDescriptionObserverIntermediate;
  friend class SetSessionDescriptionObserverIntermediate;
  friend class SetRemoteDescriptionObserverIntermediate;
  void CreateOffer();
  void AddPendingIceCandidates();
  void FailConnection(const std::string& message);

  Result<void> HandleSignalingMessageInner(const Json::Value& msg);
  Result<void> OnOfferRequestMsg(
      const std::vector<webrtc::PeerConnectionInterface::IceServer>&
          ice_servers);
  Result<void> OnOfferMsg(
      std::unique_ptr<webrtc::SessionDescriptionInterface> offer);
  Result<void> OnAnswerMsg(
      std::unique_ptr<webrtc::SessionDescriptionInterface> offer);
  Result<void> OnIceCandidateMsg(
      std::unique_ptr<webrtc::IceCandidateInterface> ice_candidate);
  Result<void> OnErrorMsg(const std::string& msg);

  webrtc::CreateSessionDescriptionObserver* ThisAsCreateSDPObserver();
  webrtc::SetSessionDescriptionObserver* ThisAsSetSDPObserver();
  rtc::scoped_refptr<webrtc::SetRemoteDescriptionObserverInterface>
  ThisAsSetRemoteSDPObserver();

  void OnCreateSDPSuccess(webrtc::SessionDescriptionInterface* desc);
  void OnCreateSDPFailure(const webrtc::RTCError& error);
  void OnSetLocalDescriptionSuccess();
  void OnSetLocalDescriptionFailure(const webrtc::RTCError& error);
  void OnSetRemoteDescriptionComplete(const webrtc::RTCError& error);

  PeerSignalingHandler& sig_handler_;
  PeerConnectionBuilder& connection_builder_;
  Observer& observer_;

  rtc::scoped_refptr<webrtc::PeerConnectionInterface> peer_connection_;
  std::vector<std::unique_ptr<webrtc::IceCandidateInterface>>
      pending_ice_candidates_;

  // To await for a connection to be established:
  std::mutex status_mtx_;
  std::condition_variable status_cond_var_;
  Result<webrtc::PeerConnectionInterface::PeerConnectionState>
      connection_status_;
};

}  // namespace webrtc_streaming
}  // namespace cuttlefish
