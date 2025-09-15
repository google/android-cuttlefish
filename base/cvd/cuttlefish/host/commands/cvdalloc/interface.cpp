/*
 * Copyright (C) 2025 The Android Open Source Project
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
#include "cuttlefish/host/commands/cvdalloc/interface.h"

#include "absl/strings/str_format.h"

namespace cuttlefish {

std::string CvdallocInterfaceName(const std::string &name, int num) {
  return absl::StrFormat("%s-%s%d", kCvdallocInterfacePrefix, name, num);
}

std::string InstanceToMobileGatewayAddress(int num) {
  return absl::StrFormat("%s.%d", kCvdallocMobileIpPrefix, 4 * num - 3);
}

std::string InstanceToMobileAddress(int num) {
  return absl::StrFormat("%s.%d", kCvdallocMobileIpPrefix, 4 * num - 2);
}

std::string InstanceToMobileBroadcast(int num) {
  return absl::StrFormat("%s.%d", kCvdallocMobileIpPrefix, 4 * num - 1);
}

std::string InstanceToWifiGatewayAddress(int num) {
  return absl::StrFormat("%s.%d", kCvdallocWirelessApIpPrefix, 4 * num - 3);
}

std::string InstanceToWifiAddress(int num) {
  return absl::StrFormat("%s.%d", kCvdallocWirelessApIpPrefix, 4 * num - 2);
}

std::string InstanceToWifiBroadcast(int num) {
  return absl::StrFormat("%s.%d", kCvdallocWirelessApIpPrefix, 4 * num - 1);
}

std::string InstanceToBridgedWifiGatewayAddress(int num) {
  return absl::StrFormat("%s.%d", kCvdallocWirelessIpPrefix, 1);
}

std::string InstanceToBridgedWifiAddress(int num) {
  return absl::StrFormat("%s.%d", kCvdallocWirelessIpPrefix, 4 * num - 2);
}

std::string InstanceToBridgedWifiBroadcast(int num) {
  return absl::StrFormat("%s.%d", kCvdallocWirelessIpPrefix, 4 * num - 1);
}

}  // namespace cuttlefish
