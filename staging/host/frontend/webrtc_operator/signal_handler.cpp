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

#include "host/frontend/webrtc_operator/signal_handler.h"

#include <android-base/logging.h>
#include <json/json.h>

#include "host/frontend/webrtc_operator/constants/signaling_constants.h"

namespace cuttlefish {

SignalHandler::SignalHandler(struct lws* wsi, DeviceRegistry* registry,
                             const ServerConfig& server_config)
    : WebSocketHandler(wsi),
      registry_(registry),
      server_config_(server_config) {}

void SignalHandler::OnConnected() {}

void SignalHandler::OnReceive(const uint8_t* msg, size_t len, bool binary) {
  if (binary) {
    LogAndReplyError("Received a binary message");
    Close();
    return;
  }
  Json::Value json_message;
  Json::CharReaderBuilder builder;
  std::unique_ptr<Json::CharReader> json_reader(builder.newCharReader());
  std::string errorMessage;
  auto str = reinterpret_cast<const char*>(msg);
  if (!json_reader->parse(str, str + len, &json_message, &errorMessage)) {
    LogAndReplyError("Received Invalid JSON");
    // Rate limiting would be a good idea here
    Close();
    return;
  }
  if (!json_message.isMember(webrtc_signaling::kTypeField) ||
      !json_message[webrtc_signaling::kTypeField].isString()) {
    LogAndReplyError("Invalid message format: '" + std::string(msg, msg + len) +
                     "'");
    // Rate limiting would be a good idea here
    Close();
    return;
  }

  auto type = json_message[webrtc_signaling::kTypeField].asString();
  handleMessage(type, json_message);
}

void SignalHandler::OnReceive(const uint8_t* msg, size_t len, bool binary,
                              bool is_final) {
  if (is_final) {
    if (receive_buffer_.empty()) {
      // no previous data - receive as-is
      OnReceive(msg, len, binary);
    } else {
      // concatenate to previous data and receive
      receive_buffer_.insert(receive_buffer_.end(), msg, msg + len);
      OnReceive(receive_buffer_.data(), receive_buffer_.size(), binary);
      receive_buffer_.clear();
    }
  } else {
    // buffer up incomplete messages
    receive_buffer_.insert(receive_buffer_.end(), msg, msg + len);
  }
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
  EnqueueMessage(reply_str.c_str(), reply_str.size());
}

void SignalHandler::Reply(const Json::Value& json) {
  Json::StreamWriterBuilder factory;
  auto replyAsString = Json::writeString(factory, json);
  EnqueueMessage(replyAsString.c_str(), replyAsString.size());
}

}  // namespace cuttlefish
