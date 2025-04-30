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
  // In substitution case the inner dirname is "cvd" -> "bin", outer dirname is
  // "bin" -> "cuttlefish-common"
  std::string bin_dir_parent =
      android::base::Dirname(android::base::Dirname(self_path));
  if (!android::base::EndsWith(bin_dir_parent, "cuttlefish-common")) {
    LOG(DEBUG) << "Binary substitution not available, run `cvd fetch` from "
                  "`cuttlefish-common` package to enable.";
    CF_EXPECTF(host_substitutions.empty(),
               "Error due to being unable to perform requested host tool "
               "substitutions. This is because `cvd` was not run from a "
               "`cuttlefish-common/bin` directory (as if from the package). "
               "`cvd` path: {}",
               self_path);
    return {};
  }

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
        Result<void> symlink_res = CreateSymLink(path, to_substitute);
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
      // TODO: schuffelen - relax this check after migration completes
      CF_EXPECTF(FileExists(to_substitute),
                 "Cannot substitute '{}', does not exist", to_substitute);
      CF_EXPECTF(unlink(to_substitute.c_str()) == 0, "{}", strerror(errno));
      CF_EXPECT(CreateSymLink(source, to_substitute));
    }
  }

  return {};
}

/**
 * cvd needs to be run from a path ending in cuttlefish-common/bin/cvd. This
 * function validates that and returns the path to the cuttlefish-common
 * directory.
 */
Result<std::string> GetCuttlefishCommonDir() {
  std::string cvd_exe = android::base::GetExecutablePath();
  CF_EXPECTF(android::base::EndsWith(cvd_exe, "cuttlefish-common/bin/cvd"),
             "Can't perform substitutions when cvd is not under "
             "cuttlefish-common/bin, it's currently at {}",
             cvd_exe);
  return cvd_exe.substr(0, cvd_exe.size() - std::string("/bin/cvd").size());
}

Result<void> Substitute(const std::string& target_dir,
                        const std::string& link_name) {
  static const std::string common_dir = CF_EXPECT(GetCuttlefishCommonDir());
  std::string target = fmt::format("{}/{}", common_dir, link_name);
  std::string full_link_name = fmt::format("{}/{}", target_dir, link_name);

  CF_EXPECTF(FileExists(target), "{}", target);
  // TODO: schuffelen - relax this check after migration completes
  CF_EXPECTF(FileExists(full_link_name),
             "Cannot substitute '{}', does not exist", full_link_name);
  CF_EXPECTF(unlink(full_link_name.c_str()) == 0, "{}", strerror(errno));
  CF_EXPECT(CreateSymLink(target, full_link_name));
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
    CF_EXPECT(Substitute(target_dir, config.symlinks(j).link_name()));
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
