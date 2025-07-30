/*
 * Copyright (C) 2022 The Android Open Source Project
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

#include "cuttlefish/host/commands/cvd/utils/common.h"

#include <mutex>
#include <ostream>
#include <string>
#include <string_view>
#include <unordered_map>

#include <android-base/file.h>
#include <android-base/logging.h>
#include <android-base/strings.h>

#include "cuttlefish/common/libs/utils/contains.h"
#include "cuttlefish/common/libs/utils/files.h"
#include "cuttlefish/common/libs/utils/result.h"
#include "cuttlefish/host/libs/config/config_utils.h"

namespace cuttlefish {

/*
 * Most branches read the kAndroidHostOut environment variable, but a few read
 * kAndroidSoongHostOut instead. Cvd will set both variables for the subtools
 * to the first value it finds:
 * - envs[kAndroidHostOut] if variable is set
 * - envs[kAndroidSoongHostOut] if variable is set
 * - envs["HOME"] if envs["HOME"] + "/bin/cvd_internal_start" exists.
 * - current working directory
 */
Result<std::string> AndroidHostPath(const cvd_common::Envs& envs) {
  auto it = envs.find(kAndroidHostOut);
  if (it != envs.end() && IsValidAndroidHostOutPath(it->second)) {
    return it->second;
  }
  it = envs.find(kAndroidSoongHostOut);
  if (it != envs.end() && IsValidAndroidHostOutPath(it->second)) {
    return it->second;
  }
  it = envs.find("HOME");
  if (it != envs.end() && IsValidAndroidHostOutPath(it->second)) {
    return it->second;
  }
  auto current_dir = CurrentDirectory();
  CF_EXPECT(IsValidAndroidHostOutPath(current_dir),
            "Unable to find a valid host tool directory.");
  return current_dir;
}

template <typename T>
std::ostream& operator<<(std::ostream& out, const std::vector<T>& v) {
  if (v.empty()) {
    out << "{}";
    return out;
  }
  if (v.size() == 1) {
    out << "{" << v.front() << "}";
    return out;
  }
  out << "{";
  for (size_t i = 0; i != v.size() - 1; i++) {
    out << v.at(i) << ", ";
  }
  out << v.back() << "}";
  return out;
}

Result<android::base::LogSeverity> EncodeVerbosity(
    const std::string& verbosity) {
  std::unordered_map<std::string, android::base::LogSeverity>
      verbosity_encode_tab{
          {"VERBOSE", android::base::VERBOSE},
          {"DEBUG", android::base::DEBUG},
          {"INFO", android::base::INFO},
          {"WARNING", android::base::WARNING},
          {"ERROR", android::base::ERROR},
          {"FATAL_WITHOUT_ABORT", android::base::FATAL_WITHOUT_ABORT},
          {"FATAL", android::base::FATAL},
      };
  CF_EXPECT(Contains(verbosity_encode_tab, verbosity),
            "Verbosity \"" << verbosity << "\" is unrecognized.");
  return verbosity_encode_tab.at(verbosity);
}

Result<std::string> VerbosityToString(
    const android::base::LogSeverity verbosity) {
  std::unordered_map<android::base::LogSeverity, std::string>
      verbosity_decode_tab{
          {android::base::VERBOSE, "VERBOSE"},
          {android::base::DEBUG, "DEBUG"},
          {android::base::INFO, "INFO"},
          {android::base::WARNING, "WARNING"},
          {android::base::ERROR, "ERROR"},
          {android::base::FATAL_WITHOUT_ABORT, "FATAL_WITHOUT_ABORT"},
          {android::base::FATAL, "FATAL"},
      };
  CF_EXPECT(Contains(verbosity_decode_tab, verbosity),
            "Verbosity \"" << verbosity << "\" is unrecognized.");
  return verbosity_decode_tab.at(verbosity);
}

static std::mutex verbosity_mutex;

android::base::LogSeverity SetMinimumVerbosity(
    const android::base::LogSeverity severity) {
  std::lock_guard lock(verbosity_mutex);
  return android::base::SetMinimumLogSeverity(severity);
}

Result<android::base::LogSeverity> SetMinimumVerbosity(
    const std::string& severity) {
  std::lock_guard lock(verbosity_mutex);
  return SetMinimumVerbosity(CF_EXPECT(EncodeVerbosity(severity)));
}

android::base::LogSeverity GetMinimumVerbosity() {
  std::lock_guard lock(verbosity_mutex);
  return android::base::GetMinimumLogSeverity();
}

std::string CvdDir() {
  return "/tmp/cvd";
}

std::string PerUserDir() { return fmt::format("{}/{}", CvdDir(), getuid()); }

std::string PerUserCacheDir() {
  return fmt::format("{}/{}/cache", CvdDir(), getuid());
}

std::string InstanceDatabasePath() {
  return fmt::format("{}/instance_database.binpb", PerUserDir());
}

std::string InstanceLocksPath() {
  return "/tmp/acloud_cvd_temp/";
}

std::string DefaultBaseDir() {
  auto time = std::chrono::system_clock::now().time_since_epoch().count();
  return fmt::format("{}/{}", PerUserDir(), time);
}

Result<std::string> GroupDirFromHome(std::string_view dir) {
  std::string per_user_dir = PerUserDir();
  // Just in case it has a / at the end, ignore result
  while (android::base::ConsumeSuffix(&dir, "/")) {}
  CF_EXPECTF(android::base::ConsumeSuffix(&dir, "/home"),
             "Unexpected group home directory: {}", dir);
  return std::string(dir);
}

std::string AssemblyDirFromHome(const std::string& group_home_dir) {
  return group_home_dir + "/cuttlefish/assembly";
}


}  // namespace cuttlefish
