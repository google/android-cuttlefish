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

#include <stddef.h>
#include <sys/stat.h>

#include <functional>
#include <iostream>
#include <list>
#include <map>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "absl/strings/str_split.h"
#include "android-base/file.h"

#include "cuttlefish/common/libs/utils/files.h"
#include "cuttlefish/files/copy_with_attributes.h"
#include "cuttlefish/files/link_or_copy.h"
#include "cuttlefish/files/recursively_remove_directory.h"
#include "cuttlefish/flag_parser/flag.h"
#include "cuttlefish/flag_parser/gflags_compat.h"
#include "cuttlefish/posix/stat.h"
#include "cuttlefish/posix/symlink.h"
#include "cuttlefish/result/expect.h"
#include "cuttlefish/result/result_type.h"

namespace cuttlefish {
namespace {

Result<void> InsertNewPair(
    std::map<std::string, std::string, std::less<void>>& map,
    std::string_view key_equals_value) {
  std::pair<std::string_view, std::string_view> pair =
      absl::StrSplit(key_equals_value, absl::MaxSplits('=', 1));
  CF_EXPECT(!pair.first.empty());
  CF_EXPECT(!pair.second.empty());
  CF_EXPECTF(map.emplace(pair.first, pair.second).second,
             "Duplicate key '{}' in '{}'", pair.first, key_equals_value);
  return {};
}

class Args {
 public:
  static Result<Args> Parse(std::vector<std::string> strs) {
    Args args;
    ConsumeFlagsOpts opts = {.fail_on_unexpected_argument = true};
    CF_EXPECT(ConsumeFlags(args.Flags(), std::move(strs), opts));
    return args;
  }

  const std::string& BaseDir() const { return base_dir_; }

  const std::map<std::string, std::string, std::less<void>>& PackageToSrc()
      const {
    return package_to_src_;
  }

  const std::map<std::string, std::string, std::less<void>>&
  PackageFileSymlinkToPackageFile() const {
    return package_file_symlink_to_package_file_;
  }

 private:
  Args() = default;

  std::vector<Flag> Flags() {
    return {
        GflagsCompatFlag("base_dir", base_dir_),
        Flag::StringFlag("package_file_to_src")
            .Setter(std::bind_front(InsertNewPair, std::ref(package_to_src_))),
        Flag::StringFlag("package_file_symlink_to_package_file")
            .Setter(std::bind_front(
                InsertNewPair,
                std::ref(package_file_symlink_to_package_file_))),
    };
  }

  std::string base_dir_;
  std::map<std::string, std::string, std::less<void>> package_to_src_;
  std::map<std::string, std::string, std::less<void>>
      package_file_symlink_to_package_file_;
};

std::string GetRelativePathForLink(std::string_view target_path,
                                   std::string_view link_path) {
  std::list<std::string_view> target = absl::StrSplit(target_path, "/");
  std::list<std::string_view> link = absl::StrSplit(link_path, "/");
  while (!target.empty() && !link.empty() && *target.begin() == *link.begin()) {
    target.pop_front();
    link.pop_front();
  }
  for (size_t i = 0; i < link.size() - 1; ++i) {
    target.push_front("..");
  }
  return absl::StrJoin(target, "/");
}

Result<void> PackagerMain(std::vector<std::string> args_strs) {
  const Args args = CF_EXPECT(Args::Parse(std::move(args_strs)));

  CF_EXPECT(RecursivelyRemoveDirectory(args.BaseDir()));

  for (const auto& [pkg_path, src_path] : args.PackageToSrc()) {
    const std::string base_pkg = absl::StrCat(args.BaseDir(), "/", pkg_path);
    CF_EXPECT(EnsureDirectoryExists(android::base::Dirname(base_pkg)));
    struct stat st = CF_EXPECT(Stat(src_path));
    // bin/cvd is sensitive to location, uses readlink("/proc/self/exe").
    // Otherwise, the input may be either a file in the source tree or a bazel
    // artifact. If we hard link the file in the source tree, bazel will try to
    // chmod it ( https://github.com/bazelbuild/bazel/issues/5588 ). In the case
    // of a hard link it will propagate to the source directory, which can be
    // surprising. As a heuristic, this tries to distinguish source files from
    // generated artifacts by seeing if the permissions are already what bazel
    // assigns to generated artifacts. If the source file looks like a bazel
    // artifact, it is copied instead of hard linked.
    if (pkg_path == "bin/cvd" || (st.st_mode & 0777) != 0555) {
      CF_EXPECT(CopyWithAttributes(src_path, base_pkg));
    } else {
      CF_EXPECT(LinkOrCopy(src_path, base_pkg));
    }
  }

  for (const auto& [sym, src] : args.PackageFileSymlinkToPackageFile()) {
    const std::string base_sym = absl::StrCat(args.BaseDir(), "/", sym);
    CF_EXPECT(EnsureDirectoryExists(android::base::Dirname(base_sym)));
    CF_EXPECT(Symlink(GetRelativePathForLink(src, sym), base_sym));
  }

  return {};
}

}  // namespace
}  // namespace cuttlefish

int main(int argc, char** argv) {
  std::vector<std::string> args(argv + 1, argv + argc);
  cuttlefish::Result<void> res = cuttlefish::PackagerMain(std::move(args));
  if (!res.has_value()) {
    std::cerr << res.error();
    return 1;
  }
  return 0;
}
