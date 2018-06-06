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

#include "host/libs/config/host_config.h"

#include <cstdlib>
#include <cstring>
#include <string>
#include <iomanip>
#include <sstream>

#include <gflags/gflags.h>

constexpr char kDefaultUuidPrefix[] = "699acfc4-c8c4-11e7-882b-5065f31dc1";

DEFINE_string(domain, vsoc::GetDefaultShmClientSocketPath(),
              "Path to the ivshmem client socket");
DEFINE_string(uuid, vsoc::GetPerInstanceDefault(kDefaultUuidPrefix).c_str(),
              "UUID to use for the device. Random if not specified");

namespace {

int InstanceFromEnvironment() {
  static constexpr char kInstanceEnvironmentVariable[] = "CUTTLEFISH_INSTANCE";
  static constexpr char kVsocUserPrefix[] = "vsoc-";
  static constexpr int kDefaultInstance = 1;

  // CUTTLEFISH_INSTANCE environment variable
  const char * instance_str = std::getenv(kInstanceEnvironmentVariable);
  if (!instance_str) {
    // Try to get it from the user instead
    instance_str = std::getenv("USER");
    if (!instance_str || std::strncmp(instance_str, kVsocUserPrefix,
                                      sizeof(kVsocUserPrefix) - 1)) {
      // No user or we don't recognize this user
      return kDefaultInstance;
    }
    instance_str += sizeof(kVsocUserPrefix) - 1;
    // Set the environment variable so that child processes see it
    setenv(kInstanceEnvironmentVariable, instance_str, 0);
  }

  int instance = std::atoi(instance_str);
  if (instance <= 0) {
    instance = kDefaultInstance;
  }

  return instance;
}

} // namespace

int vsoc::GetInstance() {
  static int instance = InstanceFromEnvironment();
  return instance;
}

std::string vsoc::GetPerInstanceDefault(const char* prefix) {
  std::ostringstream stream;
  stream << prefix << std::setfill('0') << std::setw(2) << GetInstance();
  return stream.str();
}

int vsoc::GetPerInstanceDefault(int base) {
  return base + GetInstance() - 1;
}

std::string vsoc::GetDefaultPerInstanceDir() {
  std::ostringstream stream;
  stream << "/var/run/libvirt-" << kDefaultUuidPrefix;
  return vsoc::GetPerInstanceDefault(stream.str().c_str());
}

std::string vsoc::GetDefaultPerInstancePath(const std::string& basename) {
  std::ostringstream stream;
  stream << GetDefaultPerInstanceDir() << "/" << basename;
  return stream.str();
}

std::string vsoc::GetDefaultShmClientSocketPath() {
  return vsoc::GetDefaultPerInstancePath("ivshmem_socket_client");
}

std::string vsoc::GetDomain() {
  return FLAGS_domain;
}
