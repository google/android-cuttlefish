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

#pragma once

#include <memory>
#include <string>

#include "webrtc/ServerState.h"
#include "webrtc/client_handler.h"
#include "webrtc/ws_connection.h"

class SigServerHandler
    : public WsConnectionObserver,
      public std::enable_shared_from_this<WsConnectionObserver> {
 public:
  SigServerHandler(const std::string& device_id,
                   std::shared_ptr<ServerState> server_state);
  ~SigServerHandler() override = default;

  void OnOpen() override;
  void OnClose() override;
  void OnError(const std::string& error) override;
  void OnReceive(const uint8_t* msg, size_t length, bool is_binary) override;

  void Connect(const std::string& server_addr, int server_port,
               const std::string& server_path,
               WsConnection::Security security);

 private:
  std::shared_ptr<ServerState> server_state_;
  std::shared_ptr<WsConnection> server_connection_;
  std::map<int, std::shared_ptr<ClientHandler>> clients_;
  std::string device_id_;
};
