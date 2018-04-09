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

#include "common/vsoc/shm/base.h"
#include "common/vsoc/shm/circqueue.h"
#include "common/vsoc/shm/lock.h"
#include "common/vsoc/shm/version.h"

// Memory layout for wifi packet exchange region.
namespace vsoc {
namespace layout {
namespace wifi {

struct WifiExchangeLayout : public RegionLayout {
  // Traffic originating from host that proceeds towards guest.
  CircularPacketQueue<16, 8192> guest_ingress;
  // Traffic originating from guest that proceeds towards host.
  CircularPacketQueue<16, 8192> guest_egress;

  // Desired MAC address for guest device.
  uint8_t guest_mac_address[6];
  // MAC address of host device.
  uint8_t host_mac_address[6];

  static const char* region_name;
};

ASSERT_SHM_COMPATIBLE(WifiExchangeLayout, wifi);

}  // namespace wifi
}  // namespace layout
}  // namespace vsoc
