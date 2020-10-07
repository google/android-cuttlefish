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

#include "host/libs/config/kernel_args.h"

#include <array>
#include <sstream>
#include <string>
#include <vector>

#include "common/libs/utils/files.h"
#include "host/libs/config/cuttlefish_config.h"
#include "host/libs/vm_manager/vm_manager.h"

template<typename T>
static void AppendVector(std::vector<T>* destination, const std::vector<T>& source) {
  destination->insert(destination->end(), source.begin(), source.end());
}

template<typename S, typename T>
static std::string concat(const S& s, const T& t) {
  std::ostringstream os;
  os << s << t;
  return os.str();
}

static std::string mac_to_str(const std::array<unsigned char, 6>& mac) {
  std::ostringstream stream;
  stream << std::hex << (int) mac[0];
  for (int i = 1; i < 6; i++) {
    stream << ":" << std::hex << (int) mac[i];
  }
  return stream.str();
}

std::vector<std::string> KernelCommandLineFromConfig(const cuttlefish::CuttlefishConfig& config,
    const cuttlefish::CuttlefishConfig::InstanceSpecific& instance) {
  std::vector<std::string> kernel_cmdline;

  AppendVector(&kernel_cmdline, config.vm_manager_kernel_cmdline());
  AppendVector(&kernel_cmdline, config.boot_image_kernel_cmdline());
  auto vmm = cuttlefish::vm_manager::GetVmManager(config.vm_manager());
  AppendVector(&kernel_cmdline, vmm->ConfigureGpuMode(config.gpu_mode()));
  AppendVector(&kernel_cmdline, vmm->ConfigureBootDevices());

  if (config.enable_gnss_grpc_proxy()) {
    kernel_cmdline.push_back("gnss_cmdline.serdev=serial8250/serial0/serial0-0");
    kernel_cmdline.push_back("gnss_cmdline.type=0");
    kernel_cmdline.push_back("serdev_ttyport.pdev_tty_port=ttyS1");
  }

  kernel_cmdline.push_back(concat("androidboot.serialno=", instance.serial_number()));
  kernel_cmdline.push_back(concat("androidboot.lcd_density=", config.dpi()));
  kernel_cmdline.push_back(concat(
      "androidboot.setupwizard_mode=", config.setupwizard_mode()));
  if (!config.use_bootloader()) {
    std::string slot_suffix;
    if (config.boot_slot().empty()) {
      slot_suffix = "_a";
    } else {
      slot_suffix = "_" + config.boot_slot();
    }
    kernel_cmdline.push_back(concat("androidboot.slot_suffix=", slot_suffix));
  }
  if (!config.guest_enforce_security()) {
    kernel_cmdline.push_back("androidboot.selinux=permissive");
  }
  if (config.guest_audit_security()) {
    kernel_cmdline.push_back("audit=1");
  } else {
    kernel_cmdline.push_back("audit=0");
  }

  if (instance.tombstone_receiver_port()) {
    kernel_cmdline.push_back(concat("androidboot.vsock_tombstone_port=", instance.tombstone_receiver_port()));
  }

  if (instance.config_server_port()) {
    kernel_cmdline.push_back(concat("androidboot.cuttlefish_config_server_port=", instance.config_server_port()));
  }

  if (instance.keyboard_server_port()) {
    kernel_cmdline.push_back(concat("androidboot.vsock_keyboard_port=", instance.keyboard_server_port()));
  }

  if (instance.touch_server_port()) {
    kernel_cmdline.push_back(concat("androidboot.vsock_touch_port=", instance.touch_server_port()));
  }

  if (config.enable_vehicle_hal_grpc_server() && instance.vehicle_hal_server_port() &&
      cuttlefish::FileExists(config.vehicle_hal_grpc_server_binary())) {
    constexpr int vehicle_hal_server_cid = 2;
    kernel_cmdline.push_back(concat("androidboot.vendor.vehiclehal.server.cid=", vehicle_hal_server_cid));
    kernel_cmdline.push_back(concat("androidboot.vendor.vehiclehal.server.port=", instance.vehicle_hal_server_port()));
  }

  if (instance.audiocontrol_server_port()) {
    kernel_cmdline.push_back(concat("androidboot.vendor.audiocontrol.server.cid=", instance.vsock_guest_cid()));
    kernel_cmdline.push_back(concat("androidboot.vendor.audiocontrol.server.port=", instance.audiocontrol_server_port()));
  }

  if (instance.frames_server_port()) {
    kernel_cmdline.push_back(concat("androidboot.vsock_frames_port=", instance.frames_server_port()));
  }

  kernel_cmdline.push_back(concat("androidboot.vsock_keymaster_port=",
                                  instance.keymaster_vsock_port()));

  kernel_cmdline.push_back(concat("androidboot.vsock_gatekeeper_port=",
                                  instance.gatekeeper_vsock_port()));

  if (config.enable_modem_simulator() &&
      instance.modem_simulator_ports() != "") {
    kernel_cmdline.push_back(concat("androidboot.modem_simulator_ports=",
                                    instance.modem_simulator_ports()));
  }

  // TODO(b/158131610): Set this in crosvm instead
  kernel_cmdline.push_back(concat("androidboot.wifi_mac_address=",
                                  mac_to_str(instance.wifi_mac_address())));

  kernel_cmdline.push_back("androidboot.verifiedbootstate=orange");

  AppendVector(&kernel_cmdline, config.extra_kernel_cmdline());

  return kernel_cmdline;
}
