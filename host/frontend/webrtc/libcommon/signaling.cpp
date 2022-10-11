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

#include "host/frontend/webrtc/libcommon/signaling.h"

#include "host/frontend/webrtc/libcommon/utils.h"

namespace cuttlefish {
namespace webrtc_streaming {

namespace {

Result<std::unique_ptr<webrtc::SessionDescriptionInterface>>
ParseSessionDescription(const std::string& type, const Json::Value& message,
                        webrtc::SdpType sdp_type) {
  CF_EXPECT(ValidateJsonObject(message, type,
                               {{"sdp", Json::ValueType::stringValue}}));
  auto remote_desc_str = message["sdp"].asString();
  auto remote_desc =
      webrtc::CreateSessionDescription(sdp_type, remote_desc_str);
  CF_EXPECT(remote_desc.get(), "Failed to parse sdp.");
  return remote_desc;
}

Result<std::unique_ptr<webrtc::IceCandidateInterface>> ParseIceCandidate(
    const std::string& type, const Json::Value& message) {
  CF_EXPECT(ValidateJsonObject(message, type,
                               {{"candidate", Json::ValueType::objectValue}}));
  auto candidate_json = message["candidate"];
  CF_EXPECT(ValidateJsonObject(candidate_json, "ice-candidate/candidate",
                               {
                                   {"sdpMid", Json::ValueType::stringValue},
                                   {"candidate", Json::ValueType::stringValue},
                                   {"sdpMLineIndex", Json::ValueType::intValue},
                               }));
  auto mid = candidate_json["sdpMid"].asString();
  auto candidate_sdp = candidate_json["candidate"].asString();
  auto line_index = candidate_json["sdpMLineIndex"].asInt();

  auto candidate =
      std::unique_ptr<webrtc::IceCandidateInterface>(webrtc::CreateIceCandidate(
          mid, line_index, candidate_sdp, nullptr /*error*/));
  CF_EXPECT(candidate.get(), "Failed to parse ICE candidate");
  return candidate;
}

}  // namespace

// Checks if the message contains an "ice_servers" array field and parses it
// into a vector of webrtc ICE servers. Returns an empty vector if the field
// isn't present.
Result<std::vector<webrtc::PeerConnectionInterface::IceServer>>
ParseIceServersMessage(const Json::Value& message) {
  // TODO actually return errors when it makes sense
  std::vector<webrtc::PeerConnectionInterface::IceServer> ret;
  if (!message.isMember("ice_servers") || !message["ice_servers"].isArray()) {
    // Log as verbose since the ice_servers field is optional in some messages
    LOG(VERBOSE)
        << "ice_servers field not present in json object or not an array";
    return ret;
  }
  auto& servers = message["ice_servers"];
  for (const auto& server : servers) {
    webrtc::PeerConnectionInterface::IceServer ice_server;
    if (!server.isMember("urls") || !server["urls"].isArray()) {
      // The urls field is required
      LOG(WARNING)
          << "ICE server specification missing urls field or not an array: "
          << server.toStyledString();
      continue;
    }
    auto urls = server["urls"];
    for (int url_idx = 0; url_idx < urls.size(); url_idx++) {
      auto url = urls[url_idx];
      if (!url.isString()) {
        LOG(WARNING) << "Non string 'urls' field in ice server: "
                     << url.toStyledString();
        continue;
      }
      ice_server.urls.push_back(url.asString());
    }
    if (server.isMember("credential") && server["credential"].isString()) {
      ice_server.password = server["credential"].asString();
    }
    if (server.isMember("username") && server["username"].isString()) {
      ice_server.username = server["username"].asString();
    }
    ret.push_back(ice_server);
  }
  return ret;
}

Result<void> HandleSignalingMessage(const Json::Value& message,
                                    SignalingObserver& observer) {
  CF_EXPECT(ValidateJsonObject(message, "",
                               {{"type", Json::ValueType::stringValue}}));
  auto type = message["type"].asString();

  if (type == "request-offer") {
    auto ice_servers = CF_EXPECT(ParseIceServersMessage(message),
                                 "Error parsing ice-servers field");
    return observer.OnOfferRequestMsg(ice_servers);
  } else if (type == "offer") {
    auto remote_desc = CF_EXPECT(
        ParseSessionDescription(type, message, webrtc::SdpType::kOffer));
    return observer.OnOfferMsg(std::move(remote_desc));
  } else if (type == "answer") {
    auto remote_desc = CF_EXPECT(
        ParseSessionDescription(type, message, webrtc::SdpType::kAnswer));
    return observer.OnAnswerMsg(std::move(remote_desc));
  } else if (type == "ice-candidate") {
    auto candidate = CF_EXPECT(ParseIceCandidate(type, message));
    return observer.OnIceCandidateMsg(std::move(candidate));
  } else {
    return CF_ERR("Unknown client message type: " + type);
  }
}

}  // namespace webrtc_streaming
}  // namespace cuttlefish
