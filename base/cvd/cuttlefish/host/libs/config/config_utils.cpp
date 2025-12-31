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

#include "cuttlefish/host/libs/config/config_utils.h"

#include <cstdlib>
#include <iomanip>
#include <sstream>
#include <string>

#include "absl/log/log.h"
#include "absl/strings/match.h"

#include "cuttlefish/common/libs/utils/contains.h"
#include "cuttlefish/common/libs/utils/environment.h"
#include "cuttlefish/common/libs/utils/files.h"
#include "cuttlefish/common/libs/utils/host_info.h"
#include "cuttlefish/common/libs/utils/in_sandbox.h"
#include "cuttlefish/common/libs/utils/random.h"
#include "cuttlefish/common/libs/utils/subprocess.h"
#include "cuttlefish/host/libs/config/config_constants.h"

namespace cuttlefish {

int InstanceFromString(std::string instance_str) {
  if (absl::StartsWith(instance_str, kVsocUserPrefix)) {
    instance_str = instance_str.substr(std::string(kVsocUserPrefix).size());
  } else if (absl::StartsWith(instance_str, kCvdNamePrefix)) {
    instance_str = instance_str.substr(std::string(kCvdNamePrefix).size());
  }

  int instance = std::stoi(instance_str);
  if (instance <= 0) {
    LOG(INFO) << "Failed to interpret \"" << instance_str << "\" as an id, "
              << "using instance id " << kDefaultInstance;
    return kDefaultInstance;
  }
  return instance;
}

int InstanceFromEnvironment() {
  std::string instance_str = StringFromEnv(kCuttlefishInstanceEnvVarName, "");
  if (instance_str.empty()) {
    // Try to get it from the user instead
    instance_str = StringFromEnv("USER", "");

    if (instance_str.empty()) {
      VLOG(0) << kCuttlefishInstanceEnvVarName
              << " and USER unset, using instance id " << kDefaultInstance;
      return kDefaultInstance;
    }
    if (!absl::StartsWith(instance_str, kVsocUserPrefix)) {
      // No user or we don't recognize this user
      VLOG(0) << "Non-vsoc user, using instance id " << kDefaultInstance;
      return kDefaultInstance;
    }
  }
  return InstanceFromString(instance_str);
}

int GetInstance() {
  static int instance_id = InstanceFromEnvironment();
  return instance_id;
}

int GetDefaultVsockCid() {
  // we assume that this function is used to configure CuttlefishConfig once
  static const int default_vsock_cid = 3 + GetInstance() - 1;
  return default_vsock_cid;
}

int GetVsockServerPort(const int base,
                       const int vsock_guest_cid /**< per instance guest cid */) {
    return base + (vsock_guest_cid - 3);
}

std::string GetGlobalConfigFileLink() {
  return StringFromEnv("HOME", ".") + "/.cuttlefish_config.json";
}

std::string ForCurrentInstance(const char* prefix) {
  std::ostringstream stream;
  stream << prefix << std::setfill('0') << std::setw(2) << GetInstance();
  return stream.str();
}

std::string RandomSerialNumber(const std::string& prefix) {
  const std::string hex_characters = "0123456789ABCDEF";
  return prefix + GenerateRandomString(hex_characters, 10);
}

std::string DefaultHostArtifactsPath(const std::string& file_name) {
  return (StringFromEnv("ANDROID_HOST_OUT", StringFromEnv("HOME", ".")) + "/") +
         file_name;
}

std::string HostBinaryDir() {
  return DefaultHostArtifactsPath("bin");
}

bool UseQemuPrebuilt() {
  const std::string target_prod_str = StringFromEnv("TARGET_PRODUCT", "");
  if (!Contains(target_prod_str, "arm")) {
    return true;
  }
  return false;
}

std::string DefaultQemuBinaryDir() {
  if (UseQemuPrebuilt()) {
    return HostBinaryDir() + "/" + HostArchStr() + "-linux-gnu/qemu";
  }
  return "/usr/bin";
}

std::string HostBinaryPath(const std::string& binary_name) {
#ifdef __ANDROID__
  return binary_name;
#else
  return HostBinaryDir() + "/" + binary_name;
#endif
}

std::string HostUsrSharePath(const std::string& file) {
  return DefaultHostArtifactsPath("usr/share/" + file);
}

std::string HostQemuBiosPath() {
  if (UseQemuPrebuilt()) {
    return DefaultHostArtifactsPath(
        "usr/share/qemu/" + HostArchStr() + "-linux-gnu");
  }
  return "/usr/share/qemu";
}

std::string DefaultGuestImagePath(const std::string& file_name) {
  return (StringFromEnv("ANDROID_PRODUCT_OUT", StringFromEnv("HOME", "."))) +
         file_name;
}

std::string DefaultEnvironmentPath(const std::string& environment_key,
                                   const std::string& default_value,
                                   const std::string& subpath) {
  return StringFromEnv(environment_key, default_value) + "/" + subpath;
}

bool IsValidAndroidHostOutPath(const std::string& path) {
  std::string start_bin_path = path + "/bin/cvd_internal_start";
  return FileExists(start_bin_path);
}

// In practice this is mostly validating that the `cuttlefish-base` debian
// package is installed, which implies that more things are present like the
// predefined network setup.
bool HostSupportsQemuCli() {
  static bool supported =
#ifdef __linux__
      InSandbox() ||
      Execute({"/usr/lib/cuttlefish-common/bin/capability_query.py",
               "qemu_cli"}) == 0;
#else
      true;
#endif
  return supported;
}

std::string GetSeccompPolicyDir() {
  std::string kSeccompDir =
      "usr/share/crosvm/" + HostArchStr() + "-linux-gnu/seccomp";
  return DefaultHostArtifactsPath(kSeccompDir);
}
}
