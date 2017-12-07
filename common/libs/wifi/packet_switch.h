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

#include <memory>
#include <set>

#include "common/libs/wifi/netlink.h"
#include "common/libs/wifi/virtual_wifi.h"
#include "common/libs/wifi/wr_client.h"
#include "common/vsoc/lib/wifi_exchange_view.h"

namespace cvd {

class PacketSwitch {
 public:
  PacketSwitch(Netlink* nl) : nl_(nl) {}
  ~PacketSwitch();

  bool Init();
  void Start();
  void Stop();

 private:
  void ProcessPacket(nl_msg* m, bool is_incoming);

  Netlink* nl_;

  std::mutex op_mutex_;
  // Started is referenced by all threads created by PacketSwitch to determine
  // whether to carry on working, or terminate.
  bool started_ = false;

  std::unique_ptr<std::thread> shm_xchg_;
  vsoc::wifi::WifiExchangeView shm_wifi_;

  PacketSwitch(const PacketSwitch&) = delete;
  PacketSwitch& operator=(const PacketSwitch&) = delete;
};

}  // namespace cvd
