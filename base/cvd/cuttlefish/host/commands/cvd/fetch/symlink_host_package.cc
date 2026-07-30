//
// Copyright (C) 2026 The Android Open Source Project
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

#include "cuttlefish/host/commands/cvd/fetch/symlink_host_package.h"

#include <string>

#include "absl/strings/str_cat.h"
#include "absl/strings/strip.h"
#include "android-base/file.h"

#include "cuttlefish/common/libs/utils/files.h"
#include "cuttlefish/posix/symlink.h"
#include "cuttlefish/result/expect.h"
#include "cuttlefish/result/result_type.h"

namespace cuttlefish {
namespace {

/**
 * cvd needs to be run from a path ending in cuttlefish-common/bin/cvd. This
 * function validates that and returns the path to the cuttlefish-common
 * directory.
 */
Result<std::string> GetCuttlefishCommonDir() {
  std::string cvd_exe = android::base::GetExecutablePath();
  CF_EXPECTF(cvd_exe.ends_with("cuttlefish-common/bin/cvd"),
             "Can't perform substitutions when cvd is not under "
             "cuttlefish-common/bin, it's currently at {}",
             cvd_exe);
  return cvd_exe.substr(0, cvd_exe.size() - std::string("/bin/cvd").size());
}

}  // namespace

// TODO(schuffelen): deduplicate with substitute.cc
Result<void> SymlinkHostPackage(const std::string& target_dir) {
  const std::string common_dir = CF_EXPECT(GetCuttlefishCommonDir());
  auto make_symlinks = [&common_dir,
                        &target_dir](const std::string& path) -> Result<void> {
    std::string_view local_path(path);
    CF_EXPECTF(absl::ConsumePrefix(&local_path, common_dir),
               "Unexpected prefix in : '{}'", local_path);

    const std::string full_target_path =
        absl::StrCat(target_dir, "/", local_path);

    if (IsDirectory(path)) {
      CF_EXPECT(EnsureDirectoryExists(full_target_path));
    } else {
      CF_EXPECT(Symlink(path, full_target_path));
    }
    return {};
  };
  CF_EXPECT(WalkDirectory(common_dir, make_symlinks));
  return {};
}

}  // namespace cuttlefish
