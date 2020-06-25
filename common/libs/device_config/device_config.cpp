/*
 * Copyright (C) 2019 The Android Open Source Project
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

#include "device_config.h"

#include <sstream>
#include <type_traits>

#include <android-base/logging.h>

namespace cuttlefish {

// TODO(jemoreira): Endianness when on arm64 guest and x86 host is a problem
// Raw data is sent through a vsocket from host to guest, this assert tries to
// ensure the binary representation of the struct is the same in both sides.
static constexpr int kRawDataSize = 68 + 16;  // ril + screen
static_assert(sizeof(DeviceConfig::RawData) == kRawDataSize &&
                  std::is_trivial<DeviceConfig::RawData>().value,
              "DeviceConfigRawData needs to be the same in host and guess, did "
              "you forget to update the size?");

namespace {

static constexpr auto kDataSize = sizeof(DeviceConfig::RawData);

}  // namespace

bool DeviceConfig::SendRawData(SharedFD fd) {
  std::size_t sent = 0;
  auto buffer = reinterpret_cast<uint8_t*>(&data_);
  while (sent < kDataSize) {
    auto bytes = fd->Write(buffer + sent, kDataSize - sent);
    if (bytes < 0) {
      // Don't log here, let the caller do it.
      return false;
    }
    sent += bytes;
  }
  return true;
}

void DeviceConfig::generate_address_and_prefix() {
  std::ostringstream ss;
  ss << ril_ipaddr() << "/" << ril_prefixlen();
  ril_address_and_prefix_ = ss.str();
}

}  // namespace cuttlefish
