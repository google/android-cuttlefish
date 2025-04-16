//
// Copyright (C) 2019 The Android Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "cuttlefish/host/commands/cvd/fetch/substitute.h"

#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <android-base/file.h>
#include <android-base/logging.h>
#include <android-base/strings.h>
#include <google/protobuf/text_format.h>

#include "cuttlefish/common/libs/utils/environment.h"
#include "cuttlefish/common/libs/utils/files.h"
#include "cuttlefish/common/libs/utils/result.h"
#include "cuttlefish/host/commands/cvd/fetch/host_pkg_migration.pb.h"

namespace cuttlefish {
namespace {

Result<void> SubstituteWithFlag(
    const std::string& target_dir,
    const std::vector<std::string>& host_substitutions) {
  std::string self_path;
  CF_EXPECT(android::base::Readlink("/proc/self/exe", &self_path));
  // One dirname is "cvd" -> "bin", second dirname is "bin" ->
  // "cuttlefish-common"
  std::string cuttlefish_common =
      android::base::Dirname(android::base::Dirname(self_path));
  CF_EXPECTF(android::base::EndsWith(cuttlefish_common, "cuttlefish-common"),
             "{}", cuttlefish_common);

  if (host_substitutions == std::vector<std::string>{"all"}) {
    bool substitution_error = false;
    std::function<bool(const std::string& path)> callback = [&cuttlefish_common, &target_dir, &substitution_error](const std::string& path) -> bool {
      std::string_view local_path(path);
      if (!android::base::ConsumePrefix(&local_path, cuttlefish_common)) {
        LOG(ERROR) << "Unexpected prefix in : '" << local_path << "'";
        substitution_error = true;
        return false;
      }
      std::string to_substitute = target_dir + std::string(local_path);
      if (FileExists(to_substitute) && !IsDirectory(to_substitute)) {
        if (unlink(to_substitute.c_str()) != 0) {
          PLOG(ERROR) << "Failed to unlink '" << to_substitute << "'";
          substitution_error = true;
          return false;
        }
        Result<void> symlink_res = CreateSymLink(path, to_substitute);
        if (!symlink_res.ok()) {
          LOG(ERROR) << symlink_res.error().FormatForEnv();
          substitution_error = true;
          return false;
        }
      }
      return true;
    };
    CF_EXPECT(WalkDirectory(cuttlefish_common, callback));
    CF_EXPECT(!substitution_error);
  } else {
    for (const std::string& substitution : host_substitutions) {
      std::string source = fmt::format("{}/{}", cuttlefish_common, substitution);
      std::string to_substitute = fmt::format("{}/{}", target_dir, substitution);
      // TODO: schuffelen - relax this check after migration completes
      CF_EXPECTF(FileExists(to_substitute),
                 "Cannot substitute '{}', does not exist", to_substitute);
      CF_EXPECTF(unlink(to_substitute.c_str()) == 0, "{}", strerror(errno));
      CF_EXPECT(CreateSymLink(source, to_substitute));
    }
  }

  return {};
}

Result<void> SubstituteWithMarker(const std::string& target_dir,
                                  const std::string& marker_file) {
  std::string content;
  CF_EXPECTF(android::base::ReadFileToString(marker_file, &content),
             "failed to read '{}'", marker_file);
  fetch::HostPkgMigrationConfig config;
  CF_EXPECT(google::protobuf::TextFormat::ParseFromString(content, &config),
            "failed parsing debian_substitution_marker file");
  for (int j = 0; j < config.symlinks_size(); j++) {
    const fetch::Symlink& symlink = config.symlinks(j);
    std::string full_link_name =
        fmt::format("{}/{}", target_dir, symlink.link_name());

    std::string target = symlink.target();
    static constexpr std::string_view kV1_2ModemSimulatorFiles =
        "/usr/lib/cuttlefish-common/modem_simulator/files/";
    static constexpr std::string_view kV1_3ModemSimulatorFiles =
        "/usr/lib/cuttlefish-common/etc/modem_simulator/files/";

    if (!FileExists(target) &&
        android::base::StartsWith(target, kV1_2ModemSimulatorFiles)) {
      target = std::string(kV1_3ModemSimulatorFiles) +
               target.substr(kV1_2ModemSimulatorFiles.size());
    }
    CF_EXPECTF(FileExists(target), "{}", target);
    // TODO: schuffelen - relax this check after migration completes
    const bool full_link_name_exists = FileExists(full_link_name);
    if (symlink.ignore_link_name_presence_check()) {
      LOG(DEBUG) << "Skipping checking for " << full_link_name
                 << " per migration config.";
      CF_EXPECTF(EnsureDirectoryExists(android::base::Dirname(full_link_name)),
                 "Failed to create directory for '{}'", full_link_name);
    } else {
      CF_EXPECTF(FileExists(full_link_name),
                 "Cannot substitute '{}', does not exist", full_link_name);
      CF_EXPECTF(unlink(full_link_name.c_str()) == 0, "{}", strerror(errno));
    }

    CF_EXPECT(CreateSymLink(target, full_link_name));
  }
  return {};
}

}  // namespace

Result<void> HostPackageSubstitution(
    const std::string& target_dir,
    const std::vector<std::string>& host_substitutions) {
  std::string marker_file = target_dir + "/etc/debian_substitution_marker";
  // Use a local debian_substitution_marker file for development purposes.
  std::optional<std::string> local_marker_file =
      StringFromEnv("LOCAL_DEBIAN_SUBSTITUTION_MARKER_FILE");
  if (local_marker_file.has_value()) {
    marker_file = local_marker_file.value();
    CF_EXPECTF(FileExists(marker_file),
               "local debian substitution marker file does not exist: {}",
               marker_file);
    LOG(INFO) << "using local debian substitution marker file: " << marker_file;
  }

  if (host_substitutions.empty() && FileExists(marker_file)) {
    CF_EXPECT(SubstituteWithMarker(target_dir, marker_file));
  } else {
    CF_EXPECT(SubstituteWithFlag(target_dir, host_substitutions));
  }

  return {};
}

}  // namespace cuttlefish
