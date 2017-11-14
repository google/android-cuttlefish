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

#include <condition_variable>
#include <memory>
#include <thread>
#include <vector>

#include <netlink/msg.h>

namespace avd {
constexpr int kWifiSimVersion = 1;

class Cmd {
 public:
  Cmd();
  ~Cmd();

  // Cmd() creates netlink request to be sent to kernel.
  // Returns netlink message header structure.
  nl_msg* Msg() const { return msg_; }

  // Responses() holds execution until netlink responds to this message.
  // Returns all netlink replies.
  const std::vector<nl_msg*> Responses() const;

  // OnResponse() handles data response from netlink.
  // Returns value indicating 'done' state:
  // - false, if more data is expected, or
  // - true, if processing is complete and instance can be disposed of.
  bool OnResponse(nl_msg* msg);

 private:
  nl_msg* msg_;
  std::vector<nl_msg*> responses_;

  mutable std::mutex ready_mutex_;
  mutable std::condition_variable ready_signal_;

  Cmd(const Cmd&) = delete;
  Cmd& operator=(const Cmd&) = delete;
};

}  // namespace avd
