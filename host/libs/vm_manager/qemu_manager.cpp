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

#include "common/libs/utils/subprocess.h"
#include "host/libs/config/cuttlefish_config.h"

DEFINE_string(qemu_binary,
              "/usr/bin/qemu-system-x86_64",
              "The qemu binary to use");

namespace vm_manager {

namespace {

std::string GetMonitorPath() {
  return vsoc::CuttlefishConfig::Get()->PerInstancePath("qemu_monitor.sock");
}

void LogAndSetEnv(const char* key, const std::string& value) {
  setenv(key, value.c_str(), 1);
  LOG(INFO) << key << "=" << value;
}

int BuildAndRunQemuCmd() {
  auto config = vsoc::CuttlefishConfig::Get();
  // Set the config values in the environment
  LogAndSetEnv("qemu_binary", FLAGS_qemu_binary);
  LogAndSetEnv("instance_name", config->instance_name());
  LogAndSetEnv("memory_mb", std::to_string(config->memory_mb()));
  LogAndSetEnv("cpus", std::to_string(config->cpus()));
  LogAndSetEnv("uuid", config->uuid());
  LogAndSetEnv("monitor_path",
                      config->PerInstancePath("qemu_monitor.sock"));
  LogAndSetEnv("kernel_image_path", config->kernel_image_path());
  LogAndSetEnv("ramdisk_image_path", config->ramdisk_image_path());
  LogAndSetEnv("kernel_args", config->kernel_args());
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
  return cvd::execute({vsoc::DefaultHostArtifactsPath("bin/cf_qemu.sh")});
}

}  // namespace

bool QemuManager::Start() const {
  // Create a thread that will make the launcher abort if the qemu process
  // crashes, this avoids having the launcher waiting forever for
  // VIRTUAL_DEVICE_BOOT_COMPLETED in this cases.
  std::thread waiting_thread([]() {
    int status = BuildAndRunQemuCmd();
    if (status != 0) {
      LOG(FATAL) << "Qemu process exited prematurely";
    } else {
      LOG(ERROR) << "Qemu process exited normally, it shouldn't happen";
    }
  });
  waiting_thread.detach();
  return true;
}
bool QemuManager::Stop() const {
  int errno_;
  int fd = socket(AF_UNIX, SOCK_STREAM, 0);
  if (fd == -1) {
    errno_ = errno;
    LOG(ERROR) << "Error creating socket: " << strerror(errno_);
    return false;
  }

  struct sockaddr_un addr;
  memset(&addr, 0, sizeof(addr));
  addr.sun_family = AF_UNIX;
  std::string monitor_path = GetMonitorPath();
  strncpy(addr.sun_path, monitor_path.c_str(), sizeof(addr.sun_path) - 1);

  if (connect(fd, (struct sockaddr*)&addr, sizeof(addr)) == -1) {
    errno_ = errno;
    LOG(ERROR) << "Error connecting to qemu monitor: " << strerror(errno_);
    return false;
  }

  char msg[] = "{\"execute\":\"qmp_capabilities\"}{\"execute\":\"quit\"}";
  ssize_t len = sizeof(msg) - 1;
  while (len > 0) {
    int tmp = TEMP_FAILURE_RETRY(write(fd, msg, len));
    if (tmp < 0) {
      LOG(ERROR) << "Error writing to socket";
    }
    len -= tmp;
  }
  // Log the reply
  char buff[1000];
  while ((len = TEMP_FAILURE_RETRY(read(fd, buff, sizeof(buff) - 1))) > 0) {
    buff[len] = '\0';
    LOG(INFO) << "From qemu monitor: " << buff;
  }

  return true;
}

}  // namespace vm_manager
