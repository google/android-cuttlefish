/*
 * Copyright (C) 2022 The Android Open Source Project
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

#pragma once

#include <functional>
#include <memory>
#include <thread>

#include "common/libs/fs/shared_fd.h"

namespace cuttlefish {

class ProxyServer {
 public:
  ProxyServer(std::string label, SharedFD server, std::function<SharedFD()> clients_factory);
  void Join();
  ~ProxyServer();

 private:
  SharedFD stop_fd_;
  std::thread server_;
};

// Executes a TCP proxy
// Accept() is called on the server in a loop, for every client connection a
// target connection is created through the conn_factory callback and data is
// forwarded between the two connections.
// This function is meant to execute forever, but will return if the server is
// closed in another thread. It's recommended the caller disables the default
// behavior for SIGPIPE before calling this function, otherwise it runs the risk
// or crashing the process when a connection breaks.
void Proxy(std::string label, SharedFD server, std::function<SharedFD()> conn_factory);
std::unique_ptr<ProxyServer> ProxyAsync(std::string label, SharedFD server,
                                        std::function<SharedFD()> conn_factory);

}  // namespace cuttlefish
