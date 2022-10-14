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

#include "host/frontend/webrtc/libcommon/utils.h"

#include <functional>
#include <map>

#include <json/json.h>

namespace cuttlefish {
namespace webrtc_streaming {

namespace {

Result<void> ValidateField(const Json::Value& obj, const std::string& type,
                           const std::string& field_name,
                           const Json::ValueType& field_type, bool required) {
  CF_EXPECT(obj.isObject(), "Expected object with name-value pairs");
  if (!obj.isMember(field_name) && !required) {
    return {};
  }
  if (!(obj.isMember(field_name) &&
        obj[field_name].isConvertibleTo(field_type))) {
    std::string error_msg = "Expected a field named '";
    error_msg += field_name + "' of type '";
    error_msg += std::to_string(field_type);
    error_msg += "'";
    if (!type.empty()) {
      error_msg += " in message of type '" + type + "'";
    }
    error_msg += ".";
    return CF_ERR(error_msg);
  }
  return {};
}

template <typename T>
Json::Value ToArray(const std::vector<T>& vec,
                    std::function<Json::Value(const T&)> to_json) {
  Json::Value arr(Json::ValueType::arrayValue);
  for (const auto& t : vec) {
    arr.append(to_json(t));
  }
  return arr;
}

}  // namespace

Result<void> ValidateJsonObject(
    const Json::Value& obj, const std::string& type,
    const std::map<std::string, Json::ValueType>& required_fields,
    const std::map<std::string, Json::ValueType>& optional_fields) {
  for (const auto& field_spec : required_fields) {
    CF_EXPECT(
        ValidateField(obj, type, field_spec.first, field_spec.second, true));
  }
  for (const auto& field_spec : optional_fields) {
    CF_EXPECT(
        ValidateField(obj, type, field_spec.first, field_spec.second, false));
  }
  return {};
}

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

Result<std::string> ParseError(const std::string& type,
                               const Json::Value& message) {
  CF_EXPECT(ValidateJsonObject(message, type,
                               {{"error", Json::ValueType::stringValue}}));
  return message["error"].asString();
}

Result<std::vector<webrtc::PeerConnectionInterface::IceServer>>
ParseIceServersMessage(const Json::Value& message) {
  std::vector<webrtc::PeerConnectionInterface::IceServer> ret;
  if (!message.isMember("ice_servers") || !message["ice_servers"].isArray()) {
    // The ice_servers field is optional in some messages
    LOG(VERBOSE)
        << "ice_servers field not present in json object or not an array";
    return ret;
  }
  auto& servers = message["ice_servers"];
  for (const auto& server : servers) {
    webrtc::PeerConnectionInterface::IceServer ice_server;
    CF_EXPECT(server.isMember("urls") && server["urls"].isArray(),
              "ICE server specification missing urls field or not an array: "
                  << server.toStyledString());
    auto urls = server["urls"];
    for (int url_idx = 0; url_idx < urls.size(); url_idx++) {
      auto url = urls[url_idx];
      CF_EXPECT(url.isString(), "Non string 'urls' field in ice server: "
                                    << url.toStyledString());
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

Json::Value GenerateIceServersMessage(
    const std::vector<webrtc::PeerConnectionInterface::IceServer>&
        ice_servers) {
  return ToArray<webrtc::PeerConnectionInterface::IceServer>(
      ice_servers,
      [](const webrtc::PeerConnectionInterface::IceServer& ice_server) {
        Json::Value server;
        server["urls"] = ToArray<std::string>(
            ice_server.urls,
            [](const std::string& url) { return Json::Value(url); });
        server["credential"] = ice_server.password;
        server["username"] = ice_server.username;
        return server;
      });
}

}  // namespace webrtc_streaming
}  // namespace cuttlefish
