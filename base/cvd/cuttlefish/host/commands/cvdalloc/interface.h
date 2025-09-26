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
#pragma once

#include <string>

namespace cuttlefish {

constexpr std::string_view kCvdallocInterfacePrefix = "cvd-pi";
constexpr std::string_view kCvdallocEthernetBridgeName = "cvd-pi-ebr";
constexpr std::string_view kCvdallocWirelessBridgeName = "cvd-pi-wbr";
constexpr char kCvdallocMobileIpPrefix[] = "192.168.144";
constexpr char kCvdallocWirelessIpPrefix[] = "192.168.160";
constexpr char kCvdallocWirelessApIpPrefix[] = "192.168.176";
constexpr char kCvdallocEthernetIpPrefix[] = "192.168.192";

std::string CvdallocInterfaceName(const std::string &name, int num);
std::string InstanceToMobileGatewayAddress(int num);
std::string InstanceToMobileAddress(int num);
std::string InstanceToMobileBroadcast(int num);
std::string InstanceToWifiGatewayAddress(int num);
std::string InstanceToWifiAddress(int num);
std::string InstanceToWifiBroadcast(int num);
std::string InstanceToBridgedWifiGatewayAddress(int num);
std::string InstanceToBridgedWifiAddress(int num);
std::string InstanceToBridgedWifiBroadcast(int num);

}  // namespace cuttlefish
