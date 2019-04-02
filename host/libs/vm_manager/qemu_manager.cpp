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

}  // namespace

const std::string QemuManager::name() { return "qemu_cli"; }

QemuManager::QemuManager(const vsoc::CuttlefishConfig* config)
  : VmManager(config) {}

cvd::Command QemuManager::StartCommand(){
  // Set the config values in the environment
  LogAndSetEnv("qemu_binary", config_->qemu_binary());
  LogAndSetEnv("instance_name", config_->instance_name());
  LogAndSetEnv("memory_mb", std::to_string(config_->memory_mb()));
  LogAndSetEnv("cpus", std::to_string(config_->cpus()));
  LogAndSetEnv("uuid", config_->uuid());
  LogAndSetEnv("monitor_path", GetMonitorPath(config_));
  LogAndSetEnv("kernel_image_path", config_->GetKernelImageToUse());
  LogAndSetEnv("gdb_flag", config_->gdb_flag());
  LogAndSetEnv("ramdisk_image_path", config_->ramdisk_image_path());
  LogAndSetEnv("kernel_cmdline", config_->kernel_cmdline_as_string());
  LogAndSetEnv("dtb_path", config_->dtb_path());
  LogAndSetEnv("system_image_path", config_->system_image_path());
  LogAndSetEnv("data_image_path", config_->data_image_path());
  LogAndSetEnv("cache_image_path", config_->cache_image_path());
  LogAndSetEnv("vendor_image_path", config_->vendor_image_path());
  LogAndSetEnv("metadata_image_path", config_->metadata_image_path());
  LogAndSetEnv("product_image_path", config_->product_image_path());
  LogAndSetEnv("wifi_tap_name", config_->wifi_tap_name());
  LogAndSetEnv("mobile_tap_name", config_->mobile_tap_name());
  LogAndSetEnv("kernel_log_socket_name",
                      config_->kernel_log_socket_name());
  LogAndSetEnv("console_path", config_->console_path());
  LogAndSetEnv("logcat_path", config_->logcat_path());
  LogAndSetEnv("ivshmem_qemu_socket_path",
                      config_->ivshmem_qemu_socket_path());
  LogAndSetEnv("ivshmem_vector_count",
                      std::to_string(config_->ivshmem_vector_count()));
  LogAndSetEnv("usb_v1_socket_name", config_->usb_v1_socket_name());
  LogAndSetEnv("vsock_guest_cid", std::to_string(config_->vsock_guest_cid()));
  LogAndSetEnv("logcat_mode", config_->logcat_mode());

  cvd::Command qemu_cmd(vsoc::DefaultHostArtifactsPath("bin/cf_qemu.sh"));
  return qemu_cmd;
}
bool QemuManager::Stop() {
  auto monitor_path = GetMonitorPath(config_);
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

}  // namespace vm_manager
