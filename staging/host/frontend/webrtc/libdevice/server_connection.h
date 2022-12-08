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

#include <string.h>

#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <json/json.h>
#include <libwebsockets.h>

namespace cuttlefish {
namespace webrtc_streaming {

struct ServerConfig {
  enum class Security {
    kInsecure,
    kAllowSelfSigned,
    kStrict,
  };

  // The ip address or domain name of the operator server.
  std::string addr;
  int port;
  // The path component of the operator server's register url.
  std::string path;
  // The security level to use when connecting to the operator server.
  Security security;
};

class ServerConnectionObserver {
 public:
  virtual ~ServerConnectionObserver() = default;
  // Called when the connection to the server has been established. This is the
  // cue to start using Send().
  virtual void OnOpen() = 0;
  virtual void OnClose() = 0;
  // Called when the connection to the server has failed with an unrecoverable
  // error.
  virtual void OnError(const std::string& error) = 0;
  virtual void OnReceive(const uint8_t* msg, size_t length, bool is_binary) = 0;
};

// Represents a connection to the signaling server. When a connection is created
// it connects with the server automatically but sends no info.
// Only Send() can be called from multiple threads simultaneously. Reconnect(),
// Send() and the destructor will run into race conditions if called
// concurrently.
class ServerConnection {
 public:
  static std::unique_ptr<ServerConnection> Connect(
      const ServerConfig& conf,
      std::weak_ptr<ServerConnectionObserver> observer);

  // Destroying the connection will disconnect from the signaling server and
  // release any open fds.
  virtual ~ServerConnection() = default;

  // Sends data to the server encoded as JSON.
  virtual bool Send(const Json::Value&) = 0;

  // makes re-connect request
  virtual void Reconnect();

 private:
  virtual void Connect() = 0;
};

}  // namespace webrtc_streaming
}  // namespace cuttlefish
