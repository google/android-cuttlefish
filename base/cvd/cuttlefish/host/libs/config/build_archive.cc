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

#include "cuttlefish/host/libs/config/build_archive.h"

#include <stddef.h>

#include <functional>
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
#include "android-base/file.h"
#include "fmt/ostream.h"
#include "fmt/ranges.h"

#include "cuttlefish/common/libs/utils/files.h"
#include "cuttlefish/common/libs/utils/result.h"
#include "cuttlefish/host/libs/config/fetcher_config.h"
#include "cuttlefish/host/libs/config/file_source.h"
#include "cuttlefish/host/libs/zip/libzip_cc/archive.h"
#include "cuttlefish/host/libs/zip/libzip_cc/readable_source.h"
#include "cuttlefish/host/libs/zip/zip_file.h"
#include "cuttlefish/host/libs/zip/zip_string.h"

namespace cuttlefish {
namespace {

Result<std::set<std::string, std::less<void>>> ZipMembers(ReadableZip& zip) {
  std::set<std::string, std::less<void>> members;
  size_t zip_entries = CF_EXPECT(zip.NumEntries());
  for (size_t i = 0; i < zip_entries; i++) {
    members.emplace(CF_EXPECT(zip.EntryName(i)));
  }
  return members;
}

}  // namespace

Result<BuildArchive> BuildArchive::FromFetcherConfig(
    const FetcherConfig& fetcher_config, FileSource source,
    std::string_view archive) {
  std::optional<ReadableZip> zip_file;
  std::set<std::string, std::less<void>> members;
  std::map<std::string, std::string, std::less<void>> extracted_members;

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

    members.emplace(archive_path);
    extracted_members.emplace(archive_path, path);
  }

  if (zip_file.has_value()) {
    std::set<std::string, std::less<void>> zip_members =
        CF_EXPECT(ZipMembers(*zip_file));
    members.insert(zip_members.begin(), zip_members.end());
  }

  return BuildArchive(source, std::move(extracted_members), std::move(members),
                      std::move(zip_file));
}

Result<BuildArchive> BuildArchive::FromZip(ReadableZip zip_file) {
  std::set<std::string, std::less<void>> members =
      CF_EXPECT(ZipMembers(zip_file));
  return BuildArchive({}, {}, std::move(members), std::move(zip_file));
}

Result<BuildArchive> BuildArchive::FromZipPath(const std::string& path) {
  ReadableZip zip = CF_EXPECT(ZipOpenRead(path));
  return CF_EXPECT(FromZip(std::move(zip)));
}

BuildArchive::BuildArchive(
    std::optional<FileSource> source,
    std::map<std::string, std::string, std::less<void>> extracted,
    std::set<std::string, std::less<void>> members,
    std::optional<ReadableZip> zip_file)
    : source_(source),
      extracted_(std::move(extracted)),
      members_(std::move(members)),
      zip_file_(std::move(zip_file)) {}

const std::set<std::string, std::less<void>>& BuildArchive::Members() const {
  return members_;
}

Result<std::string> BuildArchive::MemberFilepath(
    std::string_view member_name, std::optional<std::string_view> extract_dir) {
  CF_EXPECTF(members_.count(member_name), "'{}' not in archive", member_name);
  if (auto it = extracted_.find(member_name); it != extracted_.end()) {
    return it->second;
  }
  CF_EXPECT(zip_file_.has_value(), "'{}' not extracted, no source archive");
  CF_EXPECT(extract_dir.has_value(), "'{}' not extracted, need extract_dir");

  std::string dest_path = absl::StrCat(*extract_dir, "/", member_name);
  CF_EXPECT(EnsureDirectoryExists(android::base::Dirname(dest_path)));

  std::string member_name_str{member_name};
  CF_EXPECT(ExtractFile(*zip_file_, member_name_str, dest_path));

  auto it =
      extracted_.emplace(std::move(member_name_str), std::move(dest_path));
  CF_EXPECTF(!!it.second, "Failed to insert '{}' into map", member_name);

  return it.first->second;
}

Result<std::string> BuildArchive::MemberContents(std::string_view name) {
  CF_EXPECTF(members_.count(name), "'{}' not in archive", name);
  if (auto it = extracted_.find(name); it != extracted_.end()) {
    std::string contents;
    CF_EXPECTF(android::base::ReadFileToString(it->second, &contents),
               "Failed to read '{}'", it->second);
    return contents;
  }
  CF_EXPECT(zip_file_.has_value(), "'{}' not extracted, no source archive");

  ReadableZipSource reader = CF_EXPECT(zip_file_->GetFile(std::string(name)));
  return CF_EXPECT(ReadToString(reader));
}

std::ostream& operator<<(std::ostream& out, const BuildArchive& build_archive) {
  out << "BuildArchive {\n";
  if (build_archive.source_.has_value()) {
    fmt::print(out, "\tsource: '{}'\n,", *build_archive.source_);
  }
  fmt::print(out, "\textracted_members: [{}]\n",
             fmt::join(build_archive.extracted_, ", "));
  fmt::print(out, "\tmembers: [{}]\n", fmt::join(build_archive.members_, ", "));
  if (build_archive.zip_file_.has_value()) {
    out << "\tzip: present\n";
  }
  return out << "}";
}

}  // namespace cuttlefish
