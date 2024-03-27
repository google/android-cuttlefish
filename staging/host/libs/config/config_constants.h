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

namespace cuttlefish {

inline constexpr char kLogcatSerialMode[] = "serial";
inline constexpr char kLogcatVsockMode[] = "vsock";

inline constexpr char kDefaultUuidPrefix[] =
    "699acfc4-c8c4-11e7-882b-5065f31dc1";
inline constexpr char kCuttlefishConfigEnvVarName[] = "CUTTLEFISH_CONFIG_FILE";
inline constexpr char kCuttlefishInstanceEnvVarName[] = "CUTTLEFISH_INSTANCE";
inline constexpr char kVsocUserPrefix[] = "vsoc-";
inline constexpr char kCvdNamePrefix[] = "cvd-";
inline constexpr char kBootStartedMessage[] = "VIRTUAL_DEVICE_BOOT_STARTED";
inline constexpr char kBootPendingMessage[] = "VIRTUAL_DEVICE_BOOT_PENDING";
inline constexpr char kBootCompletedMessage[] = "VIRTUAL_DEVICE_BOOT_COMPLETED";
inline constexpr char kBootFailedMessage[] = "VIRTUAL_DEVICE_BOOT_FAILED";
inline constexpr char kMobileNetworkConnectedMessage[] =
    "VIRTUAL_DEVICE_NETWORK_MOBILE_CONNECTED";
inline constexpr char kWifiConnectedMessage[] =
    "VIRTUAL_DEVICE_NETWORK_WIFI_CONNECTED";
inline constexpr char kEthernetConnectedMessage[] =
    "VIRTUAL_DEVICE_NETWORK_ETHERNET_CONNECTED";
// TODO(b/131864854): Replace this with a string less likely to change
inline constexpr char kAdbdStartedMessage[] =
    "init: starting service 'adbd'...";
inline constexpr char kFastbootdStartedMessage[] =
    "init: starting service 'fastbootd'...";
inline constexpr char kFastbootStartedMessage[] =
    "Listening for fastboot command on tcp";
inline constexpr char kScreenChangedMessage[] = "VIRTUAL_DEVICE_SCREEN_CHANGED";
inline constexpr char kDisplayPowerModeChangedMessage[] =
    "VIRTUAL_DEVICE_DISPLAY_POWER_MODE_CHANGED";
inline constexpr char kInternalDirName[] = "internal";
inline constexpr char kGrpcSocketDirName[] = "grpc_socket";
inline constexpr char kSharedDirName[] = "shared";
inline constexpr char kLogDirName[] = "logs";
inline constexpr char kCrosvmVarEmptyDir[] = "/var/empty";
inline constexpr char kKernelLoadedMessage[] = "] Linux version";
inline constexpr char kBootloaderLoadedMessage[] = "U-Boot 20";
inline constexpr char kApName[] = "crosvm_openwrt";

inline constexpr int kDefaultInstance = 1;

}
