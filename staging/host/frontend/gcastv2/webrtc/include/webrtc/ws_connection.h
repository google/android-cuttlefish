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

#include "libwebsockets.h"

class WsConnectionObserver {
 public:
  virtual ~WsConnectionObserver() = default;
  virtual void OnOpen() = 0;
  virtual void OnClose() = 0;
  virtual void OnError(const std::string& error) = 0;
  virtual void OnReceive(const uint8_t* msg, size_t length, bool is_binary) = 0;
};

class WsConnection {
 public:
  enum class Security {
    kInsecure,
    kAllowSelfSigned,
    kStrict,
  };

  static std::shared_ptr<WsConnection> Create();

  virtual ~WsConnection() = default;

  virtual void Connect() = 0;

  virtual bool Send(const uint8_t* data, size_t len, bool binary = false) = 0;

 protected:
  WsConnection() = default;
};

class WsConnectionContext {
 public:
  static std::shared_ptr<WsConnectionContext> Create();

  virtual ~WsConnectionContext() = default;

  virtual std::shared_ptr<WsConnection> CreateConnection(
      int port, const std::string& addr, const std::string& path,
      WsConnection::Security secure,
      std::weak_ptr<WsConnectionObserver> observer) = 0;

 protected:
  WsConnectionContext() = default;
};
