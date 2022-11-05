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

#include <map>
#include <memory>
#include <vector>

#include <json/json.h>

#include <api/peer_connection_interface.h>

#include "common/libs/utils/result.h"

namespace cuttlefish {
namespace webrtc_streaming {

// Helper method to ensure a json object has the required fields convertible
// to the appropriate types.
Result<void> ValidateJsonObject(
    const Json::Value& obj, const std::string& type,
    const std::map<std::string, Json::ValueType>& required_fields,
    const std::map<std::string, Json::ValueType>& optional_fields = {});

// Parses a session description object from a JSON message.
Result<std::unique_ptr<webrtc::SessionDescriptionInterface>>
ParseSessionDescription(const std::string& type, const Json::Value& message,
                        webrtc::SdpType sdp_type);

// Parses an IceCandidate from a JSON message.
Result<std::unique_ptr<webrtc::IceCandidateInterface>> ParseIceCandidate(
    const std::string& type, const Json::Value& message);

// Parses a JSON error message.
Result<std::string> ParseError(const std::string& type,
                               const Json::Value& message);

// Checks if the message contains an "ice_servers" array field and parses it
// into a vector of webrtc ICE servers. Returns an empty vector if the field
// isn't present.
Result<std::vector<webrtc::PeerConnectionInterface::IceServer>>
ParseIceServersMessage(const Json::Value& message);

// Generates a JSON message from a list of ICE servers.
Json::Value GenerateIceServersMessage(
    const std::vector<webrtc::PeerConnectionInterface::IceServer>& ice_servers);

}  // namespace webrtc_streaming
}  // namespace cuttlefish
