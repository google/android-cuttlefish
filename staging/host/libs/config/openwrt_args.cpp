/*
 * Copyright (C) 2023 The Android Open Source Project
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

#include "host/libs/config/openwrt_args.h"

namespace cuttlefish {

namespace {

std::string getIpAddress(int c_class, int d_class) {
  return "192.168." + std::to_string(c_class) + "." + std::to_string(d_class);
}

}  // namespace

std::unordered_map<std::string, std::string> OpenwrtArgsFromConfig(
    const CuttlefishConfig::InstanceSpecific& instance) {
  std::unordered_map<std::string, std::string> openwrt_args;
  int instance_num = cuttlefish::GetInstance();

  int c_class_base = (instance_num - 1) / 64;
  int d_class_base = (instance_num - 1) % 64 * 4;

  // IP address for OpenWRT is pre-defined in init script of android-cuttlefish
  // github repository by using tap interfaces created with the script.
  // (github) base/debian/cuttlefish-base.cuttlefish-host-resources.init
  // The command 'crosvm run' uses openwrt_args for passing the arguments into
  // /proc/cmdline of OpenWRT instance.
  // (AOSP) device/google/cuttlefish/host/commands/run_cvd/launch/open_wrt.cpp
  // In OpenWRT instance, the script 0_default_config reads /proc/cmdline so
  // that it can apply arguments defined here.
  // (AOSP) external/openwrt-prebuilts/shared/uci-defaults/0_default_config
  if (instance.use_bridged_wifi_tap()) {
    openwrt_args["bridged_wifi_tap"] = "true";
    openwrt_args["wan_gateway"] = getIpAddress(96, 1);
    // TODO(seungjaeyoo) : Remove config after using DHCP server outside OpenWRT
    // instead.
    openwrt_args["wan_ipaddr"] = getIpAddress(96, d_class_base + 2);
    openwrt_args["wan_broadcast"] = getIpAddress(96, d_class_base + 3);

  } else {
    openwrt_args["bridged_wifi_tap"] = "false";
    openwrt_args["wan_gateway"] =
        getIpAddress(94 + c_class_base, d_class_base + 1);
    openwrt_args["wan_ipaddr"] =
        getIpAddress(94 + c_class_base, d_class_base + 2);
    openwrt_args["wan_broadcast"] =
        getIpAddress(94 + c_class_base, d_class_base + 3);
  }

  return openwrt_args;
}

}  // namespace cuttlefish
