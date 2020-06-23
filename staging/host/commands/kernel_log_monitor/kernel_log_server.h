/*
 * Copyright (C) 2017 The Android Open Source Project
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

#include <stdint.h>

#include <functional>
#include <string>
#include <vector>

#include "common/libs/fs/shared_fd.h"
#include "common/libs/fs/shared_select.h"

namespace monitor {

enum BootEvent : int32_t {
  BootStarted = 0,
  BootCompleted = 1,
  BootFailed = 2,
  WifiNetworkConnected = 3,
  MobileNetworkConnected = 4,
  AdbdStarted = 5,
};

enum class SubscriptionAction {
  ContinueSubscription,
  CancelSubscription,
};

using BootEventCallback = std::function<SubscriptionAction(BootEvent)>;

// KernelLogServer manages incoming kernel log connection from QEmu. Only accept
// one connection.
class KernelLogServer {
 public:
  KernelLogServer(cuttlefish::SharedFD pipe_fd,
                  const std::string& log_name,
                  bool deprecated_boot_completed);

  ~KernelLogServer() = default;

  // BeforeSelect is Called right before Select() to populate interesting
  // SharedFDs.
  void BeforeSelect(cuttlefish::SharedFDSet* fd_read) const;

  // AfterSelect is Called right after Select() to detect and respond to changes
  // on affected SharedFDs.
  void AfterSelect(const cuttlefish::SharedFDSet& fd_read);

  void SubscribeToBootEvents(BootEventCallback callback);
 private:
  // Respond to message from remote client.
  // Returns false, if client disconnected.
  bool HandleIncomingMessage();

  cuttlefish::SharedFD pipe_fd_;
  cuttlefish::SharedFD log_fd_;
  std::string line_;
  bool deprecated_boot_completed_;
  std::vector<BootEventCallback> subscribers_;

  KernelLogServer(const KernelLogServer&) = delete;
  KernelLogServer& operator=(const KernelLogServer&) = delete;
};

}  // namespace monitor
