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

#include <json/json.h>

#include "common/libs/fs/shared_fd.h"
#include "common/libs/fs/shared_select.h"

namespace monitor {

enum Event : int32_t {
  BootStarted = 0,
  BootCompleted = 1,
  BootFailed = 2,
  WifiNetworkConnected = 3,
  MobileNetworkConnected = 4,
  AdbdStarted = 5,
  ScreenChanged = 6,
  EthernetNetworkConnected = 7,
  KernelLoaded = 8,     // BootStarted actually comes quite late in the boot.
  BootloaderLoaded = 9, /* BootloaderLoaded is the earliest possible indicator
                         * that we're booting a device.
                         */
  DisplayPowerModeChanged = 10,
  FastbootdStarted = 11
};

enum class SubscriptionAction {
  ContinueSubscription,
  CancelSubscription,
};

using EventCallback = std::function<SubscriptionAction(Json::Value)>;

// KernelLogServer manages an incoming kernel log connection from the VMM.
// Only accept one connection.
class KernelLogServer {
 public:
  KernelLogServer(cuttlefish::SharedFD pipe_fd, const std::string& log_name);

  ~KernelLogServer() = default;

  // BeforeSelect is Called right before Select() to populate interesting
  // SharedFDs.
  void BeforeSelect(cuttlefish::SharedFDSet* fd_read) const;

  // AfterSelect is Called right after Select() to detect and respond to changes
  // on affected SharedFDs.
  void AfterSelect(const cuttlefish::SharedFDSet& fd_read);

  void SubscribeToEvents(EventCallback callback);

 private:
  // Respond to message from remote client.
  // Returns false, if client disconnected.
  bool HandleIncomingMessage();

  cuttlefish::SharedFD pipe_fd_;
  cuttlefish::SharedFD log_fd_;
  std::string line_;
  std::vector<EventCallback> subscribers_;

  KernelLogServer(const KernelLogServer&) = delete;
  KernelLogServer& operator=(const KernelLogServer&) = delete;
};

}  // namespace monitor
