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

#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>

#include <netlink/msg.h>

#include "common/libs/fs/shared_fd.h"
#include "common/libs/wifi/cmd.h"

namespace cvd {

class WRClient {
 public:
  WRClient(const std::string& socket_address);
  ~WRClient() = default;

  // Init this client: open socket to wifi router.
  bool Init();

  // Get wifirouter socket used for sending and receiving messages.
  int Sock() const;

  // Send message to wifi router.
  void Send(Cmd* msg);

  // Handle incoming responses from wifi router.
  void HandleResponses();

  // Set callback receiving all asynchronous messages and responses that do not
  // have any proper recipient.
  void SetDefaultHandler(std::function<void(nl_msg*)> cb);

 private:
  // Receive & dispatch netlink response.

  std::string address_;
  int socket_ = 0;
  std::mutex in_flight_mutex_;
  // Do not use 0 as a sequence number. 0 is reserved for asynchronous
  // notifications.
  int in_flight_last_seq_ = 1;
  std::map<uint32_t, Cmd*> in_flight_;
  std::function<void(nl_msg*)> default_handler_;

  WRClient(const WRClient&) = delete;
  WRClient& operator=(const WRClient&) = delete;
};

}  // namespace cvd
