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

#include <unistd.h>

#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <android-base/file.h>
#include <android-base/logging.h>
#include <android-base/strings.h>
#include <google/protobuf/text_format.h>
#include "absl/strings/match.h"

#include "cuttlefish/common/libs/posix/strerror.h"
#include "cuttlefish/common/libs/posix/symlink.h"
#include "cuttlefish/common/libs/utils/environment.h"
#include "cuttlefish/common/libs/utils/files.h"
#include "cuttlefish/common/libs/utils/result.h"
#include "cuttlefish/host/commands/cvd/fetch/host_pkg_migration.pb.h"

namespace cuttlefish {
namespace {

/**
 * cvd needs to be run from a path ending in cuttlefish-common/bin/cvd. This
 * function validates that and returns the path to the cuttlefish-common
 * directory.
 */
Result<std::string> GetCuttlefishCommonDir() {
  std::string cvd_exe = android::base::GetExecutablePath();
  CF_EXPECTF(absl::EndsWith(cvd_exe, "cuttlefish-common/bin/cvd"),
             "Can't perform substitutions when cvd is not under "
             "cuttlefish-common/bin, it's currently at {}",
             cvd_exe);
  return cvd_exe.substr(0, cvd_exe.size() - std::string("/bin/cvd").size());
}

Result<void> Substitute(const std::string& target,
                        const std::string& full_link_name) {
  if (!FileExists(target)) {
    LOG(WARNING) << "Target file " << target << " missing; not making "
                 << "substitution " << target << " to " << full_link_name;
    return {};
  }

  if (FileExists(full_link_name)) {
    CF_EXPECTF(unlink(full_link_name.c_str()) == 0, "{}", StrError(errno));
  }

  CF_EXPECT(Symlink(target, full_link_name));
  return {};
}

Result<void> SubstituteWithFlag(
    const std::string& target_dir,
    const std::vector<std::string>& host_substitutions) {
  if (host_substitutions.empty()) {
    return {};
  }
  const std::string bin_dir_parent = CF_EXPECT(GetCuttlefishCommonDir());

  if (host_substitutions == std::vector<std::string>{"all"}) {
    bool substitution_error = false;
    std::function<bool(const std::string& path)> callback =
        [&bin_dir_parent, &target_dir,
         &substitution_error](const std::string& path) -> bool {
      std::string_view local_path(path);
      if (!android::base::ConsumePrefix(&local_path, bin_dir_parent)) {
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
        Result<void> symlink_res = Symlink(path, to_substitute);
        if (!symlink_res.ok()) {
          LOG(ERROR) << symlink_res.error().FormatForEnv();
          substitution_error = true;
          return false;
        }
      }
      return true;
    };
    CF_EXPECT(WalkDirectory(bin_dir_parent, callback));
    CF_EXPECT(!substitution_error);
  } else {
    for (const std::string& substitution : host_substitutions) {
      std::string source = fmt::format("{}/{}", bin_dir_parent, substitution);
      std::string to_substitute = fmt::format("{}/{}", target_dir, substitution);
      CF_EXPECT(Substitute(source, to_substitute));
    }
  }

  return {};
}

bool SubstituteCheckTargetExists(const fetch::HostPkgMigrationConfig& config,
                                 std::string_view target_keyword) {
  for (int j = 0; j < config.symlinks_size(); j++) {
    if (config.symlinks(j).target().find(target_keyword) != std::string::npos) {
      return true;
    }
  }
  return false;
}

Result<void> SubstituteWithMarker(const std::string& target_dir,
                                  const std::string& marker_file) {
  static constexpr std::string_view kRunCvdKeyword = "bin/run_cvd";
  static constexpr std::string_view kSensorsSimulatorKeyword =
      "bin/sensors_simulator";

  std::string content;
  CF_EXPECTF(android::base::ReadFileToString(marker_file, &content),
             "failed to read '{}'", marker_file);
  fetch::HostPkgMigrationConfig config;
  CF_EXPECT(google::protobuf::TextFormat::ParseFromString(content, &config),
            "failed parsing debian_substitution_marker file");
  auto run_cvd_substituted =
      SubstituteCheckTargetExists(config, kRunCvdKeyword);
  static const std::string common_dir = CF_EXPECT(GetCuttlefishCommonDir());
  for (int j = 0; j < config.symlinks_size(); j++) {
    // TODO(b/452945156): The sensors simulator is launched by run_cvd, so these
    // two components must always be substituted together. Between May 2025 and
    // Oct 2025 we substituted sensors_simulator alone. Restore compatibility by
    // ignoring the sensors_simulator substitute when run_cvd is not
    // substituted. This workaround can be removed once compatibility with
    // mid-2025 images is no longer critical.
    //
    // Related discussion: b/459880764.
    if (!run_cvd_substituted &&
        config.symlinks(j).target().find(kSensorsSimulatorKeyword) !=
            std::string::npos) {
      LOG(WARNING) << "Sensors simulator (" << config.symlinks(j).target()
                   << ") cannot be substituted on its own; run_cvd must be "
                      "substituted as well.";
      continue;
    }

    std::string link_name = config.symlinks(j).link_name();
    std::string target = fmt::format("{}/{}", common_dir, link_name);
    std::string full_link_name = fmt::format("{}/{}", target_dir, link_name);
    CF_EXPECT(Substitute(target, full_link_name));
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
