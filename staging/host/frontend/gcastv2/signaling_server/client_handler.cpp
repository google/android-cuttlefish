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

#include "host/frontend/gcastv2/signaling_server/client_handler.h"

#include <android-base/logging.h>

#include "host/frontend/gcastv2/signaling_server/constants/signaling_constants.h"
#include "host/frontend/gcastv2/signaling_server/device_handler.h"

namespace cvd {

ClientHandler::ClientHandler(DeviceRegistry* registry,
                             const ServerConfig& server_config)
    : SignalHandler(registry, server_config),
      device_handler_(),
      client_id_(0) {}

void ClientHandler::SendDeviceMessage(const Json::Value& device_message) {
  Json::Value message;
  message[webrtc_signaling::kTypeField] = webrtc_signaling::kDeviceMessageType;
  message[webrtc_signaling::kPayloadField] = device_message;
  Reply(message);
}

int ClientHandler::handleMessage(const std::string& type,
                                 const Json::Value& message) {
  if (type == webrtc_signaling::kConnectType) {
    return handleConnectionRequest(message);
  } else if (type == webrtc_signaling::kForwardType) {
    return handleForward(message);
  } else {
    LogAndReplyError("Unknown message type: " + type);
    return -1;
  }
}

int ClientHandler::handleConnectionRequest(const Json::Value& message) {
  if (client_id_ > 0) {
    LOG(ERROR) << "Detected attempt to connect to multiple devices over same "
                  "websocket";
    return -EINVAL;
  }
  if (!message.isMember(webrtc_signaling::kDeviceIdField) ||
      !message[webrtc_signaling::kDeviceIdField].isString()) {
    LogAndReplyError("Invalid connection request: Missing device id");
    return -EINVAL;
  }
  auto device_id = message[webrtc_signaling::kDeviceIdField].asString();
  // Always send the server config back, even if the requested device is not
  // registered. Applications may put clients on hold until the device is ready
  // to connect.
  SendServerConfig();

  auto device_handler = registry_->GetDevice(device_id);
  if (!device_handler) {
    LogAndReplyError("Connection failed: Device not found: '" + device_id +
                     "'");
    return -1;
  }

  client_id_ = device_handler->RegisterClient(shared_from_this());
  device_handler_ = device_handler;
  Json::Value device_info_reply;
  device_info_reply[webrtc_signaling::kTypeField] =
      webrtc_signaling::kDeviceInfoType;
  device_info_reply[webrtc_signaling::kDeviceInfoField] =
      device_handler->device_info();
  Reply(device_info_reply);
  return 0;
}

int ClientHandler::handleForward(const Json::Value& message) {
  if (client_id_ == 0) {
    LogAndReplyError("Forward failed: No device asociated to client");
    return 0;
  }
  if (!message.isMember(webrtc_signaling::kPayloadField)) {
    LogAndReplyError("Forward failed: No payload present in message");
    return 0;
  }
  auto device_handler = device_handler_.lock();
  if (!device_handler) {
    LogAndReplyError("Forward failed: Device disconnected");
    // Disconnect this client since the device is gone
    return -1;
  }
  device_handler->SendClientMessage(client_id_,
                                    message[webrtc_signaling::kPayloadField]);
  return 0;
}

}  // namespace cvd
