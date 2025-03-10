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

#include "host/commands/run_cvd/kernel_args.h"

#include <string>
#include <vector>

#include "host/commands/run_cvd/launch.h"
#include "host/commands/run_cvd/runner_defs.h"
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

std::vector<std::string> KernelCommandLineFromConfig(const vsoc::CuttlefishConfig& config) {
  std::vector<std::string> kernel_cmdline;

  AppendVector(&kernel_cmdline, config.boot_image_kernel_cmdline());
  AppendVector(&kernel_cmdline,
               vm_manager::VmManager::ConfigureGpuMode(config.vm_manager(), config.gpu_mode()));
  AppendVector(&kernel_cmdline, vm_manager::VmManager::ConfigureBootDevices(config.vm_manager()));

  kernel_cmdline.push_back(concat("androidboot.serialno=", config.serial_number()));
  kernel_cmdline.push_back(concat("androidboot.lcd_density=", config.dpi()));
  if (config.logcat_mode() == cvd::kLogcatVsockMode) {
    kernel_cmdline.push_back(concat("androidboot.vsock_logcat_port=", config.logcat_vsock_port()));
  }
  kernel_cmdline.push_back(concat(
      "androidboot.cuttlefish_config_server_port=", config.config_server_port()));
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
  kernel_cmdline.push_back(concat("loop.max_part=", config.loop_max_part()));
  if (config.guest_enforce_security()) {
    kernel_cmdline.push_back("enforcing=1");
  } else {
    kernel_cmdline.push_back("enforcing=0");
    kernel_cmdline.push_back("androidboot.selinux=permissive");
  }
  if (config.guest_audit_security()) {
    kernel_cmdline.push_back("audit=1");
  } else {
    kernel_cmdline.push_back("audit=0");
  }

  AppendVector(&kernel_cmdline, config.extra_kernel_cmdline());

  return kernel_cmdline;
}

std::vector<std::string> KernelCommandLineFromVnc(const VncServerPorts& vnc_ports) {
  std::vector<std::string> kernel_args;
  if (vnc_ports.frames_server_vsock_port) {
    kernel_args.push_back(concat("androidboot.vsock_frames_port=",
                                 *vnc_ports.frames_server_vsock_port));
  }
  if (vnc_ports.touch_server_vsock_port) {
    kernel_args.push_back(concat("androidboot.vsock_touch_port=",
                                 *vnc_ports.touch_server_vsock_port));
  }
  if (vnc_ports.keyboard_server_vsock_port) {
    kernel_args.push_back(concat("androidboot.vsock_keyboard_port=",
                                 *vnc_ports.keyboard_server_vsock_port));
  }
  return kernel_args;
}

std::vector<std::string> KernelCommandLineFromTombstone(const TombstoneReceiverPorts& tombstone) {
  if (!tombstone.server_vsock_port) {
    return { "androidboot.tombstone_transmit=0" };
  }
  return {
    "androidboot.tombstone_transmit=1",
    concat("androidboot.vsock_tombstone_port=", *tombstone.server_vsock_port),
  };
}
