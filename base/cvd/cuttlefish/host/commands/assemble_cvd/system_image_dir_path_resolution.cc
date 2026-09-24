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

#include "cuttlefish/host/commands/assemble_cvd/system_image_dir_path_resolution.h"

#include <string>
#include <string_view>
#include <vector>

#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "absl/strings/str_split.h"
#include "absl/strings/strip.h"

#include "cuttlefish/files/file_exists.h"
#include "cuttlefish/result/expect.h"
#include "cuttlefish/result/result_type.h"

namespace cuttlefish {
namespace {

constexpr std::string_view kPathToken = "path=";

}  // namespace

Result<std::string> ResolveSystemImageDirPath(std::string_view value,
                                              std::string_view system_image_dir,
                                              std::string_view flag_name) {
  // An empty value means "not set"; an absolute value is the caller's own
  // choice and is left alone so that existing usage cannot regress.
  if (value.empty() || value.front() == '/') {
    return std::string(value);
  }
  std::string resolved = absl::StrCat(system_image_dir, "/", value);
  CF_EXPECTF(FileExists(resolved), "--{} file does not exist: '{}'", flag_name,
             resolved);
  return resolved;
}

Result<std::string> ResolveCrosvmDeviceTreeOverlayPath(
    std::string_view value, std::string_view system_image_dir) {
  if (value.empty()) {
    return std::string(value);
  }
  // Everything after the first comma is the optional sub-key suffix
  // ("filter", "select-symbols=[a,b]").
  // Treat them as opaque strings here.
  std::vector<std::string> segments =
      absl::StrSplit(value, absl::MaxSplits(',', 1));
  std::string_view key;
  std::string_view path = segments[0];
  if (absl::ConsumePrefix(&path, kPathToken)) {
    // The path was given with its key, so only what follows the key is a path.
    key = kPathToken;
  } else if (absl::StrContains(path, '=')) {
    // Some other property was given first. Which of the remaining tokens holds
    // the path cannot be told apart from the rest, so nothing is rewritten and
    // the value reaches crosvm exactly as the caller wrote it.
    return std::string(value);
  }
  std::string resolved = CF_EXPECT(ResolveSystemImageDirPath(
      path, system_image_dir, "crosvm_device_tree_overlay"));
  segments[0] = absl::StrCat(key, resolved);
  return absl::StrJoin(segments, ",");
}

Result<std::string> ResolveCrosvmFileBackedMappingPath(
    std::string_view value, std::string_view system_image_dir) {
  if (value.empty()) {
    return std::string(value);
  }
  // The format of file-backed-mapping of crosvm is
  // addr=NUM,size=NUM,path=PATH,offset=NUM,rw,sync,align
  // Every property in this grammar is a flat "key=value" pair or a bare
  // switch, so splitting on commas and rejoining in the same order is lossless.
  std::vector<std::string> tokens = absl::StrSplit(value, ',');
  for (std::string& token : tokens) {
    if (!absl::StartsWith(token, kPathToken)) {
      continue;
    }
    std::string resolved = CF_EXPECT(ResolveSystemImageDirPath(
        std::string_view(token).substr(kPathToken.size()), system_image_dir,
        "crosvm_file_backed_mapping"));
    token = absl::StrCat(kPathToken, resolved);
    return absl::StrJoin(tokens, ",");
  }
  // No `path=` token: there is nothing this function is allowed to rewrite, so
  // the value reaches crosvm exactly as the caller wrote it.
  return std::string(value);
}

}  // namespace cuttlefish
