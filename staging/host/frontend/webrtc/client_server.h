//
// Copyright (C) 2021 The Android Open Source Project
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

#pragma once

#include <atomic>
#include <memory>
#include <string>
#include <thread>

#include <libwebsockets.h>

namespace cuttlefish {
// Utility class to serve the client files in a thread
class ClientFilesServer {
 public:
  ~ClientFilesServer();

  static std::unique_ptr<ClientFilesServer> New(const std::string& dir);

  int port() const;

 private:
  struct Config;

  ClientFilesServer(std::unique_ptr<Config> config, lws_context* context);

  void Serve();

  std::unique_ptr<Config> config_;
  lws_context* context_;
  std::atomic<bool> running_;
  std::thread server_thread_;
};
}  // namespace cuttlefish
