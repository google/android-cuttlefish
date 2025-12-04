//
// Copyright (C) 2025 The Android Open Source Project
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

#include "cuttlefish/host/commands/assemble_cvd/android_build/find_build_archive.h"

#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/strings/match.h"

#include "cuttlefish/common/libs/utils/files.h"
#include "cuttlefish/common/libs/utils/result.h"
#include "cuttlefish/host/libs/config/build_archive.h"
#include "cuttlefish/host/libs/config/fetcher_config.h"
#include "cuttlefish/host/libs/config/file_source.h"
#include "cuttlefish/host/libs/zip/libzip_cc/archive.h"
#include "cuttlefish/host/libs/zip/zip_file.h"

namespace cuttlefish {
namespace {

bool NameMatches(std::string_view name, std::string_view pattern) {
  return absl::EndsWith(name, ".zip") && absl::StrContains(name, pattern);
}

}  // namespace

Result<BuildArchive> FindBuildArchive(const FetcherConfig& config,
                                      FileSource source,
                                      std::string_view pattern) {
  std::optional<std::string_view> archive_name;
  for (const auto& [file_name, cvd_file] : config.get_cvd_files()) {
    if (cvd_file.source != source) {
      continue;
    }
    for (std::string_view name :
         {cvd_file.archive_source, cvd_file.file_path}) {
      if (NameMatches(name, pattern)) {
        CF_EXPECTF(!archive_name.has_value() || *archive_name == name,
                   "Multiple files match '{}': '{}' and '{}'", pattern,
                   *archive_name, name);
        archive_name = name;
      }
    }
  }
  CF_EXPECTF(archive_name.has_value(), "No archive found with '{}'", pattern);
  return CF_EXPECT(
      BuildArchive::FromFetcherConfig(config, source, *archive_name));
}

Result<BuildArchive> FindBuildArchive(const std::string& directory_path,
                                      std::string_view pattern) {
  std::vector<std::string> contents =
      CF_EXPECT(DirectoryContents(directory_path));

  std::optional<std::string_view> archive_name;
  for (std::string_view member : contents) {
    if (!NameMatches(member, pattern)) {
      continue;
    }
    CF_EXPECTF(!archive_name.has_value(),
               "Found two matching files for '{}' in '{}': '{}' and '{}'",
               pattern, directory_path, *archive_name, member);
    archive_name = member;
  }
  CF_EXPECTF(archive_name.has_value(), "Could not find file with '{}' in '{}'",
             pattern, directory_path);

  ReadableZip zip = CF_EXPECT(ZipOpenRead(std::string(*archive_name)));
  return CF_EXPECT(BuildArchive::FromZip(std::move(zip)));
}

}  // namespace cuttlefish
