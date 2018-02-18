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

#include <stdlib.h>
#include <string.h>
#include <string>
#include <iomanip>
#include <sstream>

#include <gflags/gflags.h>

const char kVsocUserPrefix[] = "vsoc-";
const char kDefaultUuidPrefix[] = "699acfc4-c8c4-11e7-882b-5065f31dc1";

int GetDefaultInstance() {
  char* user = getenv("USER");
  if (user && !memcmp(user, kVsocUserPrefix, sizeof(kVsocUserPrefix) - 1)) {
    int temp = atoi(user + sizeof(kVsocUserPrefix) - 1);
    if (temp > 0) {
      return temp;
    }
  }
  return 1;
}

DEFINE_string(domain, vsoc::GetDefaultShmClientSocketPath(),
              "Path to the ivshmem client socket");
DEFINE_int32(instance, GetDefaultInstance(),
             "Instance number. Must be unique.");
DEFINE_string(uuid, vsoc::GetPerInstanceDefault(kDefaultUuidPrefix).c_str(),
              "UUID to use for the device. Random if not specified");

std::string vsoc::GetPerInstanceDefault(const char* prefix) {
  std::ostringstream stream;
  stream << prefix << std::setfill('0') << std::setw(2)
         << GetDefaultInstance();
  return stream.str();
}

int vsoc::GetPerInstanceDefault(int base) {
  return base + GetDefaultInstance() - 1;
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
