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

#include <netlink/genl/genl.h>

#include "host/commands/wifi_relay/cmd.h"

namespace cvd {

class NlClient {
 public:
  NlClient(int nl_type);
  ~NlClient() = default;

  // Init this client: set up callback & open socket.
  bool Init();

  // Get netlink socket used for sending and receiving messages.
  nl_sock* Sock() const;

  // Send message to netlink. Supplied callback will be invoked when response is
  // received.
  void Send(Cmd* msg);

  // Set callback receiving all asynchronous messages and responses that do not
  // have any proper recipient.
  // This is useful in situations, where netlink sends asynchronous event
  // notifications, such as new MAC80211 HWSIM frame.
  void SetDefaultHandler(std::function<void(nl_msg*)> cb);

 private:
  // Receive & dispatch netlink response.
  int OnResponse(nl_msg* msg);

  int nl_type_;

  std::unique_ptr<nl_cb, void (*)(nl_cb*)> callback_;
  std::unique_ptr<nl_sock, void (*)(nl_sock*)> sock_;
  std::mutex in_flight_mutex_;
  std::map<uint32_t, Cmd*> in_flight_;
  std::function<void(nl_msg*)> default_handler_;

  NlClient(const NlClient&) = delete;
  NlClient& operator=(const NlClient&) = delete;
};

}  // namespace cvd
