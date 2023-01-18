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

#include "common/libs/utils/socket2socket_proxy.h"

#include <sys/socket.h>

#include <cstring>
#include <functional>
#include <memory>
#include <ostream>
#include <string>
#include <thread>

#include <android-base/logging.h>

namespace cuttlefish {
namespace {

void Forward(const std::string& label, SharedFD from, SharedFD to) {
  LOG(DEBUG) << "[" << label << "] Proxy thread started. Starting copying data";
  auto success = to->CopyAllFrom(*from);
  if (!success) {
    if (from->GetErrno()) {
      LOG(ERROR) << "[" << label << "] Error reading: " << from->StrError();
    }
    if (to->GetErrno()) {
      LOG(ERROR) << "[" << label << "] Error writing: " << to->StrError();
    }
  }
  to->Shutdown(SHUT_WR);
  LOG(DEBUG) << "[" << label << "] Proxy thread completed";
}

void SetupProxying(const std::string& label, SharedFD client, SharedFD target) {
  std::thread([&label, client, target]() {
    LOG(DEBUG) << "[" << label << "] Launching proxy thread";
    std::thread client2target(Forward, label, client, target);
    Forward(label, target, client);
    client2target.join();
    // The actual proxying is handled in a detached thread so that this function
    // returns immediately
  }).detach();
}

}  // namespace

void Proxy(const std::string& label, SharedFD server, std::function<SharedFD()> conn_factory) {
  while (server->IsOpen()) {
    auto client = SharedFD::Accept(*server);
    if (!client->IsOpen()) {
      LOG(ERROR) << "[" << label << "] Failed to accept incoming connection: "
                 << client->StrError();
      continue;
    }
    auto target = conn_factory();
    if (target->IsOpen()) {
      SetupProxying(label, client, target);
    } else {
      LOG(ERROR) << "[" << label << "] Cannot connect to the target to setup proxying: "
                 << target->StrError();
    }
    // The client will close when it goes out of scope here if the target didn't
    // open.
  }
  LOG(INFO) << "[" << label << "] Proxying ended: " << server->StrError();
}

}  // namespace cuttlefish
