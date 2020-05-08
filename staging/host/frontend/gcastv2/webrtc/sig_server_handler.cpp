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
//

#include "host/frontend/gcastv2/webrtc/include/webrtc/sig_server_handler.h"

#include <android-base/strings.h>
#include <gflags/gflags.h>
#include <json/json.h>

#include <webrtc/STUNClient.h>
#include <webrtc/STUNMessage.h>
#include <webrtc/ServerState.h>
#include "Utils.h"

#include "host/frontend/gcastv2/signaling_server/constants/signaling_constants.h"

DECLARE_string(public_ip);

namespace {

constexpr auto kStreamIdField = "stream_id";
constexpr auto kXResField = "x_res";
constexpr auto kYResField = "y_res";
constexpr auto kDpiField = "dpi";
constexpr auto kIsTouchField = "is_touch";
constexpr auto kDisplaysField = "displays";

std::string FigureOutPublicIp(const std::string &stun_server) {
  if (!FLAGS_public_ip.empty() && FLAGS_public_ip != "0.0.0.0") {
    return FLAGS_public_ip;
  }

  // NOTE: We only contact the external STUN server once upon startup
  // to determine our own public IP.
  // This only works if NAT does not remap ports, i.e. a local port 15550
  // is visible to the outside world on port 15550 as well.
  // If this condition is not met, this code will have to be modified
  // and a STUN request made for each locally bound socket before
  // fulfilling a "MyWebSocketHandler::getCandidate" ICE request.

  const addrinfo kHints = {
      AI_ADDRCONFIG,
      PF_INET,
      SOCK_DGRAM,
      IPPROTO_UDP,
      0,        // ai_addrlen
      nullptr,  // ai_addr
      nullptr,  // ai_canonname
      nullptr   // ai_next
  };

  auto pieces = SplitString(stun_server, ':');
  CHECK_EQ(pieces.size(), 2u);

  addrinfo *infos;
  CHECK(!getaddrinfo(pieces[0].c_str(), pieces[1].c_str(), &kHints, &infos));

  sockaddr_storage stunAddr;
  memcpy(&stunAddr, infos->ai_addr, infos->ai_addrlen);

  freeaddrinfo(infos);
  infos = nullptr;

  CHECK_EQ(stunAddr.ss_family, AF_INET);

  std::mutex lock;
  std::condition_variable cond;
  bool done = false;

  auto runLoop = std::make_shared<RunLoop>("STUN");
  std::string public_ip;

  auto stunClient = std::make_shared<STUNClient>(
      runLoop, reinterpret_cast<const sockaddr_in &>(stunAddr),
      [&lock, &cond, &done, &public_ip](int result,
                                        const std::string &myPublicIp) {
        CHECK(!result);
        LOG(INFO) << "STUN-discovered public IP: " << myPublicIp;

        public_ip = myPublicIp;

        std::lock_guard autoLock(lock);
        done = true;
        cond.notify_all();
      });

  stunClient->run();

  std::unique_lock autoLock(lock);
  while (!done) {
    cond.wait(autoLock);
  }
  return public_ip;
}

std::string StunServerFromConfig(const Json::Value &server_config) {
  if (!server_config.isMember(cvd::webrtc_signaling::kServersField) ||
      !server_config[cvd::webrtc_signaling::kServersField].isArray()) {
    return "";
  }
  auto ice_servers = server_config[cvd::webrtc_signaling::kServersField];
  for (Json::ArrayIndex i = 0; i < ice_servers.size(); ++i) {
    if (!ice_servers[i].isMember(cvd::webrtc_signaling::kUrlsField)) {
      LOG(WARNING) << "Ice server received without a urls field";
      continue;
    }
    auto url = ice_servers[i][cvd::webrtc_signaling::kUrlsField];
    if (url.isArray()) {
      if (url.size() == 0) {
        LOG(WARNING) << "Ice server received with empty urls field";
        continue;
      }
      url = url[0];
    }
    if (!url.isString()) {
      LOG(WARNING) << "Ice server with non-string url";
      continue;
    }
    auto url_str = url.asString();
    if (::android::base::StartsWith(url_str, "stun:")) {
      return url_str.substr(std::string("stun:").size());
    }
  }
  return "";
}

void SendJson(std::shared_ptr<WsConnection> ws_conn, const Json::Value &data) {
  Json::FastWriter json_writer;
  auto data_str = json_writer.write(data);
  ws_conn->Send(reinterpret_cast<const uint8_t *>(data_str.c_str()),
                data_str.size());
}

bool ParseMessage(const uint8_t *data, size_t length, Json::Value *msg_out) {
  Json::Reader json_reader;
  auto str = reinterpret_cast<const char *>(data);
  return json_reader.parse(str, str + length, *msg_out) >= 0;
}

}  // namespace

SigServerHandler::SigServerHandler(const std::string &device_id,
                                   std::shared_ptr<ServerState> server_state)
    : server_state_(server_state), device_id_(device_id) {}

void SigServerHandler::Connect(const std::string &server_addr, int server_port,
                               const std::string &server_path,
                               WsConnection::Security security) {
  // This can be a local variable since the connection object will keep a
  // reference to it.
  auto ws_context = WsConnectionContext::Create();
  server_connection_ = ws_context->CreateConnection(
      server_port, server_addr, server_path, security, weak_from_this());

  CHECK(server_connection_) << "Unable to create websocket connection object";

  server_connection_->Connect();
}

void SigServerHandler::OnOpen() {
  auto config = vsoc::CuttlefishConfig::Get();
  Json::Value register_obj;
  register_obj[cvd::webrtc_signaling::kTypeField] =
      cvd::webrtc_signaling::kRegisterType;
  register_obj[cvd::webrtc_signaling::kDeviceIdField] =
      device_id_.empty() ? config->ForDefaultInstance().instance_name()
                         : device_id_;
  Json::Value device_info;
  Json::Value displays(Json::ValueType::arrayValue);
  Json::Value main_display;

  main_display[kStreamIdField] = "display_0";
  main_display[kXResField] = config->x_res();
  main_display[kYResField] = config->y_res();
  main_display[kDpiField] = config->dpi();
  main_display[kIsTouchField] = true;

  displays.append(main_display);
  device_info[kDisplaysField] = displays;
  register_obj[cvd::webrtc_signaling::kDeviceInfoField] = device_info;
  SendJson(server_connection_, register_obj);
}

void SigServerHandler::OnClose() {
  LOG(WARNING) << "Websocket closed unexpectedly";
}

void SigServerHandler::OnError(const std::string &error) {
  LOG(FATAL) << "Error detected on server connection: " << error;
}

void SigServerHandler::OnReceive(const uint8_t *data, size_t length,
                                 bool is_binary) {
  Json::Value server_message;
  if (is_binary || !ParseMessage(data, length, &server_message)) {
    LOG(ERROR) << "Received invalid JSON from server: '"
               << (is_binary ? std::string("(binary_data)")
                             : std::string(data, data + length))
               << "'";
    return;
  }
  if (!server_message.isMember(cvd::webrtc_signaling::kTypeField) ||
      !server_message[cvd::webrtc_signaling::kTypeField].isString()) {
    LOG(ERROR) << "No message_type field from server";
    return;
  }
  auto type = server_message[cvd::webrtc_signaling::kTypeField].asString();
  if (type == cvd::webrtc_signaling::kConfigType) {
    auto stun_server = StunServerFromConfig(server_message);
    auto public_ip =
        stun_server.empty() ? FLAGS_public_ip : FigureOutPublicIp(stun_server);
    server_state_->SetPublicIp(public_ip);
  } else if (type == cvd::webrtc_signaling::kClientMessageType) {
    if (!server_message.isMember(cvd::webrtc_signaling::kClientIdField) ||
        !server_message[cvd::webrtc_signaling::kClientIdField].isInt()) {
      LOG(ERROR) << "Client message received without valid client id";
      return;
    }
    auto client_id =
        server_message[cvd::webrtc_signaling::kClientIdField].asInt();
    if (!server_message.isMember(cvd::webrtc_signaling::kPayloadField)) {
      LOG(ERROR) << "Received empty client message";
      return;
    }
    auto client_message = server_message[cvd::webrtc_signaling::kPayloadField];
    if (clients_.count(client_id) == 0) {
      clients_[client_id].reset(new ClientHandler(
          server_state_, [this, client_id](const Json::Value &msg) {
            Json::Value wrapper;
            wrapper[cvd::webrtc_signaling::kPayloadField] = msg;
            wrapper[cvd::webrtc_signaling::kTypeField] =
                cvd::webrtc_signaling::kForwardType;
            wrapper[cvd::webrtc_signaling::kClientIdField] = client_id;
            // This is safe to call from the webrtc runloop because
            // WsConnection is thread safe
            SendJson(server_connection_, wrapper);
          }));
      clients_[client_id]->OnConnectionTimeOut([this, client_id]{
        clients_.erase(client_id);
      });
    }

    auto client_handler = clients_[client_id];

    // Client handler operations need to happen in its own runloop
    server_state_->run_loop()->post([client_handler, client_message] {
      client_handler->HandleMessage(client_message);
    });
  } else {
    LOG(ERROR) << "Unknown message type: " << type;
    return;
  }
}
