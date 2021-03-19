/*
 * Copyright (C) 2021 The Android Open Source Project
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

#include "host/libs/config/bootconfig_args.h"

#include <array>
#include <sstream>
#include <string>
#include <vector>

#include "common/libs/utils/environment.h"
#include "common/libs/utils/files.h"
#include "host/libs/config/cuttlefish_config.h"
#include "host/libs/vm_manager/crosvm_manager.h"
#include "host/libs/vm_manager/qemu_manager.h"
#include "host/libs/vm_manager/vm_manager.h"

namespace cuttlefish {

using vm_manager::CrosvmManager;
using vm_manager::QemuManager;

namespace {

template <typename T>
void AppendVector(std::vector<T>* destination, const std::vector<T>& source) {
  destination->insert(destination->end(), source.begin(), source.end());
}

template <typename S, typename T>
std::string concat(const S& s, const T& t) {
  std::ostringstream os;
  os << s << t;
  return os.str();
}

std::string mac_to_str(const std::array<unsigned char, 6>& mac) {
  std::ostringstream stream;
  stream << std::hex << (int)mac[0];
  for (int i = 1; i < 6; i++) {
    stream << ":" << std::hex << (int)mac[i];
  }
  return stream.str();
}

// TODO(schuffelen): Move more of this into host/libs/vm_manager, as a
// substitute for the vm_manager comparisons.
std::vector<std::string> VmManagerBootconfig(const CuttlefishConfig& config) {
  std::vector<std::string> vm_manager_cmdline;
  if (config.console()) {
    vm_manager_cmdline.push_back("androidboot.console=" + config.console_dev());
  } else {
    // Specify an invalid path under /dev, so the init process will disable the
    // console service due to the console not being found. On physical devices,
    // it is enough to not specify androidboot.console= *and* not specify the
    // console= kernel command line parameter, because the console and kernel
    // dmesg are muxed. However, on cuttlefish, we don't need to mux, and would
    // prefer to retain the kernel dmesg logging, so we must work around init
    // falling back to the check for /dev/console (which we'll always have).
    vm_manager_cmdline.push_back("androidboot.console=invalid");
  }
  return vm_manager_cmdline;
}

}  // namespace

std::vector<std::string> BootconfigArgsFromConfig(
    const CuttlefishConfig& config,
    const CuttlefishConfig::InstanceSpecific& instance) {
  std::vector<std::string> bootconfig_args;

  AppendVector(&bootconfig_args, VmManagerBootconfig(config));
  auto vmm = vm_manager::GetVmManager(config.vm_manager(), config.target_arch());
  bootconfig_args.push_back(
      vmm->ConfigureBootDevices(instance.virtual_disk_paths().size()));
  AppendVector(&bootconfig_args, vmm->ConfigureGpuMode(config.gpu_mode()));

  bootconfig_args.push_back(
      concat("androidboot.serialno=", instance.serial_number()));
  bootconfig_args.push_back(concat("androidboot.lcd_density=", config.dpi()));
  bootconfig_args.push_back(
      concat("androidboot.setupwizard_mode=", config.setupwizard_mode()));
  if (!config.guest_enforce_security()) {
    bootconfig_args.push_back("androidboot.selinux=permissive");
  }

  if (instance.tombstone_receiver_port()) {
    bootconfig_args.push_back(concat("androidboot.vsock_tombstone_port=",
                                     instance.tombstone_receiver_port()));
  }

  if (instance.config_server_port()) {
    bootconfig_args.push_back(
        concat("androidboot.cuttlefish_config_server_port=",
               instance.config_server_port()));
  }

  if (instance.keyboard_server_port()) {
    bootconfig_args.push_back(concat("androidboot.vsock_keyboard_port=",
                                     instance.keyboard_server_port()));
  }

  if (instance.touch_server_port()) {
    bootconfig_args.push_back(
        concat("androidboot.vsock_touch_port=", instance.touch_server_port()));
  }

  if (config.enable_vehicle_hal_grpc_server() &&
      instance.vehicle_hal_server_port() &&
      FileExists(config.vehicle_hal_grpc_server_binary())) {
    constexpr int vehicle_hal_server_cid = 2;
    bootconfig_args.push_back(concat(
        "androidboot.vendor.vehiclehal.server.cid=", vehicle_hal_server_cid));
    bootconfig_args.push_back(
        concat("androidboot.vendor.vehiclehal.server.port=",
               instance.vehicle_hal_server_port()));
  }

  if (instance.audiocontrol_server_port()) {
    bootconfig_args.push_back(
        concat("androidboot.vendor.audiocontrol.server.cid=",
               instance.vsock_guest_cid()));
    bootconfig_args.push_back(
        concat("androidboot.vendor.audiocontrol.server.port=",
               instance.audiocontrol_server_port()));
  }

  if (instance.frames_server_port()) {
    bootconfig_args.push_back(concat("androidboot.vsock_frames_port=",
                                     instance.frames_server_port()));
  }

  if (config.enable_modem_simulator() &&
      instance.modem_simulator_ports() != "") {
    bootconfig_args.push_back(concat("androidboot.modem_simulator_ports=",
                                     instance.modem_simulator_ports()));
  }

  // TODO(b/158131610): Set this in crosvm instead
  bootconfig_args.push_back(concat("androidboot.wifi_mac_address=",
                                   mac_to_str(instance.wifi_mac_address())));

  bootconfig_args.push_back("androidboot.verifiedbootstate=orange");

  // Non-native architecture implies a significantly slower execution speed, so
  // set a large timeout multiplier.
  if (!IsHostCompatible(config.target_arch())) {
    bootconfig_args.push_back("androidboot.hw_timeout_multiplier=50");
  }

  // TODO(b/173815685): Create an extra_bootconfig flag and add it to bootconfig

  return bootconfig_args;
}

}  // namespace cuttlefish
