//
// Copyright (C) 2020 The Android Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "host/frontend/gcastv2/signaling_server/signal_handler.h"

#include <android-base/logging.h>
#include <json/json.h>

#include "host/frontend/gcastv2/signaling_server/constants/signaling_constants.h"

namespace cvd {

SignalHandler::SignalHandler(DeviceRegistry* registry,
                             const ServerConfig& server_config)
    : registry_(registry), server_config_(server_config) {}

bool SignalHandler::IsBinaryMessage(uint8_t header_byte) {
  // https://tools.ietf.org/html/rfc6455#section-5.2
  return (header_byte & 0x0f) == 0x02;
}

int SignalHandler::handleMessage(uint8_t header_byte, const uint8_t* msg,
                                 size_t len) {
  if (IsBinaryMessage(header_byte)) {
    LOG(ERROR) << "Received a binary message";
    return -EINVAL;
  }
  Json::Value json_message;
  Json::Reader json_reader;
  auto str = reinterpret_cast<const char*>(msg);
  if (!json_reader.parse(str, str + len, json_message)) {
    LOG(ERROR) << "Received Invalid JSON";
    // Rate limiting would be a good idea here
    return -EINVAL;
  }
  if (!json_message.isMember(webrtc_signaling::kTypeField) ||
      !json_message[webrtc_signaling::kTypeField].isString()) {
    LogAndReplyError("Invalid message format: '" + std::string(msg, msg + len) +
                     "'");
    // Rate limiting would be a good idea here
    return -EINVAL;
  }

  auto type = json_message[webrtc_signaling::kTypeField].asString();
  return handleMessage(type, json_message);
}

void SignalHandler::SendServerConfig() {
  // Call every time to allow config changes?
  auto reply = server_config_.ToJson();
  reply[webrtc_signaling::kTypeField] = webrtc_signaling::kConfigType;
  Reply(reply);
}

void SignalHandler::LogAndReplyError(const std::string& error_message) {
  LOG(ERROR) << error_message;
  auto reply_str = "{\"error\":\"" + error_message + "\"}";
  sendMessage(reply_str.c_str(), reply_str.size());
}

void SignalHandler::Reply(const Json::Value& json) {
  Json::FastWriter json_writer;
  auto replyAsString = json_writer.write(json);
  sendMessage(replyAsString.c_str(), replyAsString.size());
}

}  // namespace cvd
