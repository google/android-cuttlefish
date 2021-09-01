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

#include "host/frontend/webrtc_operator/client_handler.h"

#include <algorithm>
#include <random>

#include <android-base/logging.h>

#include "host/frontend/webrtc_operator/constants/signaling_constants.h"
#include "host/frontend/webrtc_operator/device_handler.h"

namespace cuttlefish {

namespace {
std::string RandomClientSecret(size_t len) {
  static constexpr auto chars =
      "0123456789"
      "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
      "abcdefghijklmnopqrstuvwxyz";
  std::string ret(len, '\0');
  std::default_random_engine e{std::random_device{}()};
  std::uniform_int_distribution<int> random{
      0, static_cast<int>(std::strlen(chars)) - 1};
  std::generate_n(ret.begin(), len, [&]() { return chars[random(e)]; });
  return ret;
}
}

ClientWSHandler::ClientWSHandler(struct lws* wsi, DeviceRegistry* registry,
                             const ServerConfig& server_config)
    : SignalHandler(wsi, registry, server_config),
      device_handler_(),
      client_id_(0) {}

void ClientWSHandler::OnClosed() {
  auto device_handler = device_handler_.lock();
  if (device_handler) {
    device_handler->SendClientDisconnectMessage(client_id_);
  }
}

void ClientWSHandler::SendDeviceMessage(const Json::Value& device_message) {
  Json::Value message;
  message[webrtc_signaling::kTypeField] = webrtc_signaling::kDeviceMessageType;
  message[webrtc_signaling::kPayloadField] = device_message;
  Reply(message);
}

void ClientWSHandler::handleMessage(const std::string& type,
                                  const Json::Value& message) {
  if (type == webrtc_signaling::kConnectType) {
    handleConnectionRequest(message);
  } else if (type == webrtc_signaling::kForwardType) {
    handleForward(message);
  } else {
    LogAndReplyError("Unknown message type: " + type);
  }
}

void ClientWSHandler::handleConnectionRequest(const Json::Value& message) {
  if (client_id_ > 0) {
    LogAndReplyError(
        "Attempt to connect to multiple devices over same websocket");
    Close();
    return;
  }
  if (!message.isMember(webrtc_signaling::kDeviceIdField) ||
      !message[webrtc_signaling::kDeviceIdField].isString()) {
    LogAndReplyError("Invalid connection request: Missing device id");
    Close();
    return;
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
    Close();
    return;
  }

  client_id_ = device_handler->RegisterClient(shared_from_this());
  device_handler_ = device_handler;
  Json::Value device_info_reply;
  device_info_reply[webrtc_signaling::kTypeField] =
      webrtc_signaling::kDeviceInfoType;
  device_info_reply[webrtc_signaling::kDeviceInfoField] =
      device_handler->device_info();
  Reply(device_info_reply);
}

void ClientWSHandler::handleForward(const Json::Value& message) {
  if (client_id_ == 0) {
    LogAndReplyError("Forward failed: No device asociated to client");
    Close();
    return;
  }
  if (!message.isMember(webrtc_signaling::kPayloadField)) {
    LogAndReplyError("Forward failed: No payload present in message");
    Close();
    return;
  }
  auto device_handler = device_handler_.lock();
  if (!device_handler) {
    LogAndReplyError("Forward failed: Device disconnected");
    // Disconnect this client since the device is gone
    Close();
    return;
  }
  device_handler->SendClientMessage(client_id_,
                                    message[webrtc_signaling::kPayloadField]);
}

ClientWSHandlerFactory::ClientWSHandlerFactory(DeviceRegistry* registry,
                                           const ServerConfig& server_config)
  : registry_(registry),
    server_config_(server_config) {}

std::shared_ptr<WebSocketHandler> ClientWSHandlerFactory::Build(struct lws* wsi) {
  return std::shared_ptr<WebSocketHandler>(
      new ClientWSHandler(wsi, registry_, server_config_));
}

/******************************************************************************/

class PollConnectionHandler : public ClientHandler {
 public:
  PollConnectionHandler() = default;

  void SendDeviceMessage(const Json::Value& message) override {
    constexpr size_t kMaxMessagesInQueue = 1000;
    if (messages_.size() > kMaxMessagesInQueue) {
      LOG(ERROR) << "Polling client " << client_id_ << " reached "
                 << kMaxMessagesInQueue
                 << " messages queued. Started to drop messages.";
      return;
    }
    messages_.push_back(message);
  }

  std::vector<Json::Value> PollMessages() {
    std::vector<Json::Value> ret;
    std::swap(ret, messages_);
    return ret;
  }

  void SetDeviceHandler(std::weak_ptr<DeviceHandler> device_handler) {
    device_handler_ = device_handler;
  }

  void SetClientId(size_t client_id) { client_id_ = client_id; }

  size_t client_id() const { return client_id_; }
  std::shared_ptr<DeviceHandler> device_handler() const {
    return device_handler_.lock();
  }

 private:
  size_t client_id_ = 0;
  std::weak_ptr<DeviceHandler> device_handler_;
  std::vector<Json::Value> messages_;
};

std::shared_ptr<PollConnectionHandler> PollConnectionStore::Get(
    const std::string& conn_id) const {
  if (!handlers_.count(conn_id)) {
    return nullptr;
  }
  return handlers_.at(conn_id);
}

std::string PollConnectionStore::Add(std::shared_ptr<PollConnectionHandler> handler) {
  std::string conn_id;
  do {
    conn_id = RandomClientSecret(64);
  } while (handlers_.count(conn_id));
  handlers_[conn_id] = handler;
  return conn_id;
}

ClientDynHandler::ClientDynHandler(struct lws* wsi,
                                   PollConnectionStore* poll_store)
    : DynHandler(wsi), poll_store_(poll_store) {}

HttpStatusCode ClientDynHandler::DoGet() {
  // No message from the client uses the GET method because all of them
  // change the server state somehow
  return HttpStatusCode::MethodNotAllowed;
}

void ClientDynHandler::Reply(const Json::Value& json) {
  Json::StreamWriterBuilder factory;
  auto replyAsString = Json::writeString(factory, json);
  AppendDataOut(replyAsString);
}

void ClientDynHandler::ReplyError(const std::string& message) {
  LOG(ERROR) << message;
  Json::Value reply;
  reply["type"] = "error";
  reply["error"] = message;
  Reply(reply);
}

HttpStatusCode ClientDynHandler::DoPost() {
  auto& data = GetDataIn();
  Json::Value json_message;
  std::shared_ptr<PollConnectionHandler> poll_handler;
  if (data.size() > 0) {
    Json::CharReaderBuilder builder;
    std::unique_ptr<Json::CharReader> json_reader(builder.newCharReader());
    std::string error_message;
    if (!json_reader->parse(data.c_str(), data.c_str() + data.size(), &json_message,
                            &error_message)) {
      ReplyError("Error parsing JSON: " + error_message);
      // Rate limiting would be a good idea here
      return HttpStatusCode::BadRequest;
    }

    std::string conn_id;
    if (json_message.isMember(webrtc_signaling::kClientSecretField)) {
      conn_id =
          json_message[webrtc_signaling::kClientSecretField].asString();
      poll_handler = poll_store_->Get(conn_id);
      if (!poll_handler) {
        ReplyError("Error: Unknown connection id" + conn_id);
        return HttpStatusCode::Unauthorized;
      }
    }
  }
  return DoPostInner(poll_handler, json_message);
}

HttpStatusCode ClientDynHandler::Poll(
    std::shared_ptr<PollConnectionHandler> poll_handler) {
  if (!poll_handler) {
    ReplyError("Poll failed: No device associated to client");
    return HttpStatusCode::Unauthorized;
  }
  auto messages = poll_handler->PollMessages();
  Json::Value reply(Json::arrayValue);
  for (auto& msg : messages) {
    reply.append(msg);
  }
  Reply(reply);
  return HttpStatusCode::Ok;
}

ConnectHandler::ConnectHandler(struct lws* wsi, DeviceRegistry* registry,
                               PollConnectionStore* poll_store)
    : ClientDynHandler(wsi, poll_store), registry_(registry) {}

HttpStatusCode ConnectHandler::DoPostInner(
    std::shared_ptr<PollConnectionHandler> poll_handler,
    const Json::Value& message) {
  if (!message.isMember(webrtc_signaling::kDeviceIdField) ||
      !message[webrtc_signaling::kDeviceIdField].isString()) {
    ReplyError("Invalid connection request: Missing device id");
    return HttpStatusCode::BadRequest;
  }
  auto device_id = message[webrtc_signaling::kDeviceIdField].asString();

  auto device_handler = registry_->GetDevice(device_id);
  if (!device_handler) {
    ReplyError("Connection failed: Device not found: '" + device_id + "'");
    return HttpStatusCode::NotFound;
  }

  poll_handler = std::make_shared<PollConnectionHandler>();
  poll_handler->SetClientId(device_handler->RegisterClient(poll_handler));
  poll_handler->SetDeviceHandler(device_handler);
  auto conn_id = poll_store_->Add(poll_handler);

  Json::Value device_info_reply;
  device_info_reply[webrtc_signaling::kClientSecretField] = conn_id;
  device_info_reply[webrtc_signaling::kTypeField] =
      webrtc_signaling::kDeviceInfoType;
  device_info_reply[webrtc_signaling::kDeviceInfoField] =
      device_handler->device_info();
  Reply(device_info_reply);

  return HttpStatusCode::Ok;
}

ForwardHandler::ForwardHandler(struct lws* wsi,
                               PollConnectionStore* poll_store)
    : ClientDynHandler(wsi, poll_store) {}

HttpStatusCode ForwardHandler::DoPostInner(
    std::shared_ptr<PollConnectionHandler> poll_handler,
    const Json::Value& message) {
  if (!poll_handler) {
    ReplyError("Forward failed: No device associated to client");
    return HttpStatusCode::Unauthorized;
  }
  auto client_id = poll_handler->client_id();
  if (client_id == 0) {
    ReplyError("Forward failed: No device associated to client");
    return HttpStatusCode::Unauthorized;
  }
  if (!message.isMember(webrtc_signaling::kPayloadField)) {
    ReplyError("Forward failed: No payload present in message");
    return HttpStatusCode::BadRequest;
  }
  auto device_handler = poll_handler->device_handler();
  if (!device_handler) {
    ReplyError("Forward failed: Device disconnected");
    return HttpStatusCode::NotFound;
  }
  device_handler->SendClientMessage(client_id,
                                    message[webrtc_signaling::kPayloadField]);
  // Don't waste an HTTP session returning nothing, send any pending device
  // messages to the client instead.
  return Poll(poll_handler);
}

PollHandler::PollHandler(struct lws* wsi, PollConnectionStore* poll_store)
    : ClientDynHandler(wsi, poll_store) {}

HttpStatusCode PollHandler::DoPostInner(
    std::shared_ptr<PollConnectionHandler> poll_handler,
    const Json::Value& /*message*/) {
  return Poll(poll_handler);
}

ConfigHandler::ConfigHandler(struct lws* wsi, const ServerConfig& server_config)
    : DynHandler(wsi), server_config_(server_config) {}

HttpStatusCode ConfigHandler::DoGet() {
  Json::Value reply = server_config_.ToJson();
  reply[webrtc_signaling::kTypeField] = webrtc_signaling::kConfigType;
  Json::StreamWriterBuilder factory;
  auto replyAsString = Json::writeString(factory, reply);
  AppendDataOut(replyAsString);
  return HttpStatusCode::Ok;
}

HttpStatusCode ConfigHandler::DoPost() {
  return HttpStatusCode::MethodNotAllowed;
}

}  // namespace cuttlefish
