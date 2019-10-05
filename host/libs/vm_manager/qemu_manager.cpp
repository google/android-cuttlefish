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

#include "host/libs/vm_manager/qemu_manager.h"

#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cstdlib>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include <glog/logging.h>

#include "common/libs/fs/shared_select.h"
#include "common/libs/utils/files.h"
#include "common/libs/utils/subprocess.h"
#include "common/libs/utils/users.h"
#include "host/libs/config/cuttlefish_config.h"

namespace vm_manager {

namespace {

std::string GetMonitorPath(const vsoc::CuttlefishConfig* config) {
  return config->PerInstancePath("qemu_monitor.sock");
}

void LogAndSetEnv(const char* key, const std::string& value) {
  setenv(key, value.c_str(), 1);
  LOG(INFO) << key << "=" << value;
}

std::string JoinString(const std::vector<std::string>& args,
                       const std::string& delim) {
  bool first = true;
  std::stringstream output;
  for (const auto& arg : args) {
    if (first) {
      first = false;
    } else {
      output << delim;
    }
    output << arg;
  }
  return output.str();
}

bool Stop() {
  auto config = vsoc::CuttlefishConfig::Get();
  auto monitor_path = GetMonitorPath(config);
  auto monitor_sock = cvd::SharedFD::SocketLocalClient(
      monitor_path.c_str(), false, SOCK_STREAM);

  if (!monitor_sock->IsOpen()) {
    LOG(ERROR) << "The connection to qemu is closed, is it still running?";
    return false;
  }
  char msg[] = "{\"execute\":\"qmp_capabilities\"}{\"execute\":\"quit\"}";
  ssize_t len = sizeof(msg) - 1;
  while (len > 0) {
    int tmp = monitor_sock->Write(msg, len);
    if (tmp < 0) {
      LOG(ERROR) << "Error writing to socket: " << monitor_sock->StrError();
      return false;
    }
    len -= tmp;
  }
  // Log the reply
  char buff[1000];
  while ((len = monitor_sock->Read(buff, sizeof(buff) - 1)) > 0) {
    buff[len] = '\0';
    LOG(INFO) << "From qemu monitor: " << buff;
  }

  return true;
}

}  // namespace

const std::string QemuManager::name() { return "qemu_cli"; }

bool QemuManager::ConfigureGpu(vsoc::CuttlefishConfig *config) {
  if (config->gpu_mode() != vsoc::kGpuModeGuestSwiftshader) {
    return false;
  }
  // Override the default HAL search paths in all cases. We do this because
  // the HAL search path allows for fallbacks, and fallbacks in conjunction
  // with properities lead to non-deterministic behavior while loading the
  // HALs.
  config->add_kernel_cmdline("androidboot.hardware.gralloc=cutf_ashmem");
  config->add_kernel_cmdline(
      "androidboot.hardware.hwcomposer=cutf_ivsh_ashmem");
  config->add_kernel_cmdline(
      "androidboot.hardware.egl=swiftshader");
  return true;
}

void QemuManager::ConfigureBootDevices(vsoc::CuttlefishConfig* config) {
  // PCI domain 0, bus 0, device 3, function 0
  // This is controlled with 'addr=0x3' in cf_qemu.sh
  config->add_kernel_cmdline(
    "androidboot.boot_devices=pci0000:00/0000:00:03.0");
}

QemuManager::QemuManager(const vsoc::CuttlefishConfig* config)
  : VmManager(config) {}

std::vector<cvd::Command> QemuManager::StartCommands(bool /*with_frontend*/) {
  // Set the config values in the environment
  LogAndSetEnv("qemu_binary", config_->qemu_binary());
  LogAndSetEnv("instance_name", config_->instance_name());
  LogAndSetEnv("memory_mb", std::to_string(config_->memory_mb()));
  LogAndSetEnv("cpus", std::to_string(config_->cpus()));
  LogAndSetEnv("uuid", config_->uuid());
  LogAndSetEnv("monitor_path", GetMonitorPath(config_));
  LogAndSetEnv("kernel_image_path", config_->GetKernelImageToUse());
  LogAndSetEnv("gdb_flag", config_->gdb_flag());
  LogAndSetEnv("ramdisk_image_path", config_->final_ramdisk_path());
  LogAndSetEnv("kernel_cmdline", config_->kernel_cmdline_as_string());
  LogAndSetEnv("virtual_disk_paths", JoinString(config_->virtual_disk_paths(),
                                                ";"));
  LogAndSetEnv("wifi_tap_name", config_->wifi_tap_name());
  LogAndSetEnv("mobile_tap_name", config_->mobile_tap_name());
  LogAndSetEnv("kernel_log_pipe_name",
               config_->kernel_log_pipe_name());
  LogAndSetEnv("console_path", config_->console_path());
  LogAndSetEnv("logcat_path", config_->logcat_path());
  LogAndSetEnv("ivshmem_qemu_socket_path",
               config_->ivshmem_qemu_socket_path());
  LogAndSetEnv("ivshmem_vector_count",
               std::to_string(config_->ivshmem_vector_count()));
  LogAndSetEnv("usb_v1_socket_name", config_->usb_v1_socket_name());
  LogAndSetEnv("vsock_guest_cid", std::to_string(config_->vsock_guest_cid()));
  LogAndSetEnv("logcat_mode", config_->logcat_mode());
  LogAndSetEnv("use_bootloader", config_->use_bootloader() ? "true" : "false");
  LogAndSetEnv("bootloader", config_->bootloader());

  cvd::Command qemu_cmd(vsoc::DefaultHostArtifactsPath("bin/cf_qemu.sh"),
                        [](cvd::Subprocess* proc) {
                          auto stopped = Stop();
                          if (stopped) {
                            return true;
                          }
                          LOG(WARNING) << "Failed to stop VMM nicely, "
                                       << "attempting to KILL";
                          return KillSubprocess(proc);
                        });
  std::vector<cvd::Command> ret;
  ret.push_back(std::move(qemu_cmd));
  return ret;
}

}  // namespace vm_manager
