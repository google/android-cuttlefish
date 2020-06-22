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

#include "host/frontend/gcastv2/signaling_server/device_handler.h"

#include <android-base/logging.h>

#include "host/frontend/gcastv2/signaling_server/client_handler.h"
#include "host/frontend/gcastv2/signaling_server/constants/signaling_constants.h"

namespace cuttlefish {

DeviceHandler::DeviceHandler(DeviceRegistry* registry,
                             const ServerConfig& server_config)
    : SignalHandler(registry, server_config), device_info_(), clients_() {}

DeviceHandler::~DeviceHandler() {
  // Unregister the device when the websocket connection is closed
  if (!device_id_.empty() && registry_) {
    registry_->UnRegisterDevice(device_id_);
  }
}

size_t DeviceHandler::RegisterClient(
    std::shared_ptr<ClientHandler> client_handler) {
  clients_.emplace_back(client_handler);
  return clients_.size();
}

int DeviceHandler::handleMessage(const std::string& type,
                                 const Json::Value& message) {
  if (type == webrtc_signaling::kRegisterType) {
    return HandleRegistrationRequest(message);
  } else if (type == webrtc_signaling::kForwardType) {
    return HandleForward(message);
  } else {
    LogAndReplyError("Unknown message type: " + type);
  }

  return 0;
}

int DeviceHandler::HandleRegistrationRequest(const Json::Value& message) {
  if (!device_id_.empty()) {
    LogAndReplyError("Device already registered: " + device_id_);
    return -EINVAL;
  }
  if (!message.isMember(webrtc_signaling::kDeviceIdField) ||
      !message[webrtc_signaling::kDeviceIdField].isString() ||
      message[webrtc_signaling::kDeviceIdField].asString().empty()) {
    LogAndReplyError("Missing device id in registration request");
    return -EINVAL;
  }
  device_id_ = message[webrtc_signaling::kDeviceIdField].asString();
  if (message.isMember(webrtc_signaling::kDeviceInfoField)) {
    device_info_ = message[webrtc_signaling::kDeviceInfoField];
  }
  if (!registry_->RegisterDevice(device_id_, weak_from_this())) {
    LOG(ERROR) << "Device registration failed";
    return -1;
  }

  SendServerConfig();

  return 0;
}

int DeviceHandler::HandleForward(const Json::Value& message) {
  if (!message.isMember(webrtc_signaling::kClientIdField) || !message[webrtc_signaling::kClientIdField].isInt()) {
    LogAndReplyError("Forward failed: Missing or invalid client id");
    return 0;
  }
  size_t client_id = message[webrtc_signaling::kClientIdField].asInt();
  if (!message.isMember(webrtc_signaling::kPayloadField)) {
    LogAndReplyError("Forward failed: Missing payload");
    return 0;
  }
  if (client_id > clients_.size()) {
    LogAndReplyError("Forward failed: Unknown client " +
                     std::to_string(client_id));
    return 0;
  }
  auto client_index = client_id - 1;
  auto client_handler = clients_[client_index].lock();
  if (!client_handler) {
    LogAndReplyError("Forward failed: Client " + std::to_string(client_id) +
                     " disconnected");
    return 0;
  }
  client_handler->SendDeviceMessage(message[webrtc_signaling::kPayloadField]);
  return 0;
}

void DeviceHandler::SendClientMessage(size_t client_id,
                                      const Json::Value& client_message) {
  Json::Value msg;
  msg[webrtc_signaling::kTypeField] = webrtc_signaling::kClientMessageType;
  msg[webrtc_signaling::kClientIdField] = static_cast<Json::UInt>(client_id);
  msg[webrtc_signaling::kPayloadField] = client_message;
  Reply(msg);
}

}  // namespace cuttlefish
