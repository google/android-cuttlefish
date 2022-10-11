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
#include <vector>

#include <api/peer_connection_interface.h>
#include <json/json.h>

#include "common/libs/utils/result.h"

namespace cuttlefish {
namespace webrtc_streaming {

class SignalingObserver {
 public:
  virtual ~SignalingObserver() = default;

  virtual Result<void> OnOfferRequestMsg(
      const std::vector<webrtc::PeerConnectionInterface::IceServer>&
          ice_servers) = 0;
  virtual Result<void> OnOfferMsg(
      std::unique_ptr<webrtc::SessionDescriptionInterface> offer) = 0;
  virtual Result<void> OnAnswerMsg(
      std::unique_ptr<webrtc::SessionDescriptionInterface> offer) = 0;
  virtual Result<void> OnIceCandidateMsg(
      std::unique_ptr<webrtc::IceCandidateInterface> ice_candidate) = 0;
  virtual Result<void> OnErrorMsg(const std::string& msg) = 0;
};

Result<std::vector<webrtc::PeerConnectionInterface::IceServer>>
ParseIceServersMessage(const Json::Value& message);

Result<void> HandleSignalingMessage(const Json::Value& msg,
                                    SignalingObserver& observer);

}  // namespace webrtc_streaming
}  // namespace cuttlefish
