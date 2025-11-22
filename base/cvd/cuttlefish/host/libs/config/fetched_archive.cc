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

#include "cuttlefish/host/libs/config/fetched_archive.h"

#include <stddef.h>

#include <map>
#include <optional>
#include <ostream>
#include <set>
#include <string>
#include <string_view>
#include <utility>

#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/strip.h"
#include "fmt/ostream.h"
#include "fmt/ranges.h"

#include "cuttlefish/common/libs/utils/files.h"
#include "cuttlefish/common/libs/utils/result.h"
#include "cuttlefish/host/libs/config/fetcher_config.h"
#include "cuttlefish/host/libs/config/file_source.h"
#include "cuttlefish/host/libs/zip/libzip_cc/archive.h"
#include "cuttlefish/host/libs/zip/zip_file.h"

namespace cuttlefish {

Result<FetchedArchive> FetchedArchive::Create(
    const FetcherConfig& fetcher_config, FileSource source,
    std::string_view archive) {
  std::optional<ReadableZip> zip_file;
  std::set<std::string> members;
  std::map<std::string, std::string> extracted_members;

  // To validate `xyz.zip` only has exact matches and not `/abc-xyz.zip`.
  std::string slash_archive = absl::StrCat("/", archive);
  for (const auto& [path, member] : fetcher_config.get_cvd_files()) {
    if (member.source != source) {
      continue;
    }

    bool name_matches = path == archive || absl::EndsWith(path, slash_archive);
    if (name_matches && absl::EndsWith(archive, ".zip")) {
      zip_file = CF_EXPECT(ZipOpenRead(path));
      continue;
    }
    if (member.archive_source != archive) {
      continue;
    }

    std::string_view archive_path = member.archive_path;
    while (absl::ConsumePrefix(&archive_path, "/")) {
    }

    CF_EXPECTF(FileExists(path),
               "'{}' is present in the fetcher config not in the filesystem.",
               path);

    members.insert(std::string(archive_path));
    extracted_members.emplace(std::string(archive_path), path);
  }

  if (zip_file.has_value()) {
    size_t zip_entries = CF_EXPECT(zip_file->NumEntries());
    for (size_t i = 0; i < zip_entries; i++) {
      members.insert(CF_EXPECT(zip_file->EntryName(i)));
    }
  }

  return FetchedArchive(source, std::move(extracted_members),
                        std::move(members), std::move(zip_file));
}

FetchedArchive::FetchedArchive(
    FileSource source, std::map<std::string, std::string> extracted_members,
    std::set<std::string> members, std::optional<ReadableZip> zip_file)
    : source_(source),
      extracted_members_(std::move(extracted_members)),
      members_(std::move(members)),
      zip_file_(std::move(zip_file)) {}

const std::set<std::string>& FetchedArchive::Members() const {
  return members_;
}

Result<std::string> FetchedArchive::MemberFilepath(
    std::string_view member_name, std::optional<std::string_view> extract_dir) {
  return CF_ERR("TODO: schuffelen");
}

std::ostream& operator<<(std::ostream& out,
                         const FetchedArchive& fetched_archive) {
  out << "FetchedArchive {\n";
  fmt::print(out, "\tsource: '{}'\n",
             SourceEnumToString(fetched_archive.source_));
  fmt::print(out, "\textracted_members: [{}]\n",
             fmt::join(fetched_archive.extracted_members_, ", "));
  fmt::print(out, "\tmembers: [{}]\n",
             fmt::join(fetched_archive.members_, ", "));
  bool has_zip = fetched_archive.zip_file_.has_value();
  fmt::print(out, "\tzip: {}\n", has_zip ? "present" : "missing");
  return out << "}";
}

}  // namespace cuttlefish
