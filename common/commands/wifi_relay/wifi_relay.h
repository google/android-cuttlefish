/*
 * Copyright (C) 2018 The Android Open Source Project
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

#include "common/libs/wifi_relay/mac80211_hwsim.h"

#include <errno.h>
#include <memory>

class WifiRelay {
 public:
  WifiRelay(
          const Mac80211HwSim::MacAddress &localMAC,
          const Mac80211HwSim::MacAddress &remoteMAC);

  WifiRelay(const WifiRelay &) = delete;
  WifiRelay &operator=(const WifiRelay &) = delete;

  virtual ~WifiRelay() = default;

  int initCheck() const;

  void run();

  int mac80211Family() const;
  int nl80211Family() const;

 private:
  int init_check_ = -ENODEV;

  std::unique_ptr<Mac80211HwSim> mMac80211HwSim;
};
