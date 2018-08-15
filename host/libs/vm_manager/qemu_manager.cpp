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

#include <gflags/gflags.h>
#include <glog/logging.h>

#include "common/libs/fs/shared_select.h"
#include "common/libs/utils/files.h"
#include "common/libs/utils/subprocess.h"
#include "common/libs/utils/users.h"
#include "host/libs/config/cuttlefish_config.h"

DEFINE_string(qemu_binary,
              "/usr/bin/qemu-system-x86_64",
              "The qemu binary to use");

namespace vm_manager {

namespace {

std::string GetMonitorPath(vsoc::CuttlefishConfig* config) {
  return config->PerInstancePath("qemu_monitor.sock");
}

void LogAndSetEnv(const char* key, const std::string& value) {
  setenv(key, value.c_str(), 1);
  LOG(INFO) << key << "=" << value;
}

pid_t BuildAndRunQemuCmd(vsoc::CuttlefishConfig* config) {
  // Set the config values in the environment
  LogAndSetEnv("qemu_binary", FLAGS_qemu_binary);
  LogAndSetEnv("instance_name", config->instance_name());
  LogAndSetEnv("memory_mb", std::to_string(config->memory_mb()));
  LogAndSetEnv("cpus", std::to_string(config->cpus()));
  LogAndSetEnv("uuid", config->uuid());
  LogAndSetEnv("monitor_path", GetMonitorPath(config));
  LogAndSetEnv("kernel_image_path", config->kernel_image_path());
  LogAndSetEnv("gdb_flag", config->gdb_flag());
  LogAndSetEnv("ramdisk_image_path", config->ramdisk_image_path());
  LogAndSetEnv("kernel_args", config->kernel_args_as_string());
  LogAndSetEnv("dtb_path", config->dtb_path());
  LogAndSetEnv("system_image_path", config->system_image_path());
  LogAndSetEnv("data_image_path", config->data_image_path());
  LogAndSetEnv("cache_image_path", config->cache_image_path());
  LogAndSetEnv("vendor_image_path", config->vendor_image_path());
  LogAndSetEnv("wifi_tap_name", config->wifi_tap_name());
  LogAndSetEnv("mobile_tap_name", config->mobile_tap_name());
  LogAndSetEnv("kernel_log_socket_name",
                      config->kernel_log_socket_name());
  LogAndSetEnv("console_path", config->console_path());
  LogAndSetEnv("logcat_path", config->logcat_path());
  LogAndSetEnv("ivshmem_qemu_socket_path",
                      config->ivshmem_qemu_socket_path());
  LogAndSetEnv("ivshmem_vector_count",
                      std::to_string(config->ivshmem_vector_count()));
  LogAndSetEnv("usb_v1_socket_name", config->usb_v1_socket_name());
  return cvd::subprocess({vsoc::DefaultHostArtifactsPath("bin/cf_qemu.sh")});
}

}  // namespace

const std::string QemuManager::name() { return "qemu_cli"; }

QemuManager::QemuManager(vsoc::CuttlefishConfig* config)
  : VmManager(config) {}

bool QemuManager::Start() {
  if (monitor_conn_->IsOpen()) {
    LOG(ERROR) << "Already started, should call Stop() first";
    return false;
  }
  auto monitor_path = GetMonitorPath(config_);
  auto monitor_sock = cvd::SharedFD::SocketLocalServer(
      monitor_path.c_str(), false, SOCK_STREAM, 0666);

  BuildAndRunQemuCmd(config_);

  cvd::SharedFDSet fdset;
  fdset.Set(monitor_sock);
  struct timeval timeout {5, 0}; // Wait at most 5 seconds for qemu to connect
  int select_result = cvd::Select(&fdset, 0, 0, &timeout);
  if (select_result < 0) {
    LOG(ERROR) << "Error when calling seletct: " << strerror(errno);
    return false;
  }
  if (select_result == 0) {
    LOG(ERROR) << "Timed out waiting for qemu to connect to monitor";
    return false;
  }
  monitor_conn_ = cvd::SharedFD::Accept(*monitor_sock);
  monitor_conn_->Fcntl(F_SETFD, FD_CLOEXEC);
  return monitor_conn_->IsOpen();
}
bool QemuManager::Stop() {
  if (!monitor_conn_->IsOpen()) {
    LOG(ERROR) << "The connection to qemu is closed, is it still running?";
    return false;
  }
  char msg[] = "{\"execute\":\"qmp_capabilities\"}{\"execute\":\"quit\"}";
  ssize_t len = sizeof(msg) - 1;
  while (len > 0) {
    int tmp = monitor_conn_->Write(msg, len);
    if (tmp < 0) {
      LOG(ERROR) << "Error writing to socket: " << monitor_conn_->StrError();
      return false;
    }
    len -= tmp;
  }
  // Log the reply
  char buff[1000];
  while ((len = monitor_conn_->Read(buff, sizeof(buff) - 1)) > 0) {
    buff[len] = '\0';
    LOG(INFO) << "From qemu monitor: " << buff;
  }

  return true;
}

bool QemuManager::EnsureInstanceDirExists() const {
  auto instance_dir = config_->instance_dir();
  if (!cvd::DirectoryExists(instance_dir.c_str())) {
    LOG(INFO) << "Setting up " << instance_dir;
    if (mkdir(instance_dir.c_str(), S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH) < 0) {
      LOG(ERROR) << "Unable to create " << instance_dir << ". Error: " << errno;
      return false;
    }
  }
  return true;

}
bool QemuManager::CleanPriorFiles() const {
  std::string run_files = config_->PerInstancePath("*") + " " +
                          config_->mempath() + " " +
                          config_->cuttlefish_env_path() + " " +
                          vsoc::GetGlobalConfigFileLink();
  LOG(INFO) << "Assuming run files of " << run_files;
  std::string fuser_cmd = "fuser " + run_files + " 2> /dev/null";
  int rval = std::system(fuser_cmd.c_str());
  // fuser returns 0 if any of the files are open
  if (WEXITSTATUS(rval) == 0) {
    LOG(ERROR) << "Clean aborted: files are in use";
    return false;
  }
  std::string clean_command = "rm -rf " + run_files;
  rval = std::system(clean_command.c_str());
  if (WEXITSTATUS(rval) != 0) {
    LOG(ERROR) << "Remove of files failed";
    return false;
  }
  return true;
}

bool QemuManager::ValidateHostConfiguration(
    std::vector<std::string>* config_commands) const {
  // the check for cvdnetwork needs to happen even if the user is not in kvm, so
  // we cant just say UserInGroup("kvm") && UserInGroup("cvdnetwork")
  auto in_kvm = VmManager::UserInGroup("kvm", config_commands);
  auto in_cvdnetwork = VmManager::UserInGroup("cvdnetwork", config_commands);
  return in_kvm && in_cvdnetwork;
}
}  // namespace vm_manager
