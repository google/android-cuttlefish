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

#pragma once

#include <functional>
#include <map>
#include <optional>
#include <ostream>
#include <set>
#include <string>
#include <string_view>

#include "absl/strings/str_format.h"
#include "cuttlefish/host/libs/zip/libzip_cc/archive.h"
#include "fmt/ostream.h"

#include "cuttlefish/common/libs/utils/result.h"
#include "cuttlefish/host/libs/config/fetcher_config.h"
#include "cuttlefish/host/libs/config/file_source.h"

namespace cuttlefish {

/**
 * An archive that was downloaded by `cvd fetch`.
 *
 * The archive may be partially or completely extracted, and the archive may
 * have been deleted as part of the fetch process, leaving only extracted files.
 */
class FetchedArchive {
 public:
  static Result<FetchedArchive> Create(const FetcherConfig&, FileSource,
                                       std::string_view archive_name);

  template <typename Sink>
  friend void AbslStringify(Sink& sink, const FetchedArchive& img) {
    sink.Append(absl::FormatStreamed(img));
  }

  /**
   * Returns the filenames of the members held in the archive.
   *
   * If a subset of the archive members were extracted and the archive was
   * deleted, this may be incomplete.
   */
  const std::set<std::string, std::less<void>>& Members() const;

  /**
   * Returns the file path to a member of the archive, extracted on the
   * filesystem. If the member is not already extracted and the archive is
   * present, it will extract the file into `extract_dir`.
   *
   * Error conditions:
   *
   * - The archive does not have a member called `member_name`.
   * - The file needed to be extracted, but `extract_dir` was not present.
   * - There was a failure to extract the file member.
   */
  Result<std::string_view> MemberFilepath(
      std::string_view member_name,
      std::optional<std::string_view> extract_dir);

  Result<std::string> MemberContents(std::string_view name);

  friend std::ostream& operator<<(std::ostream&, const FetchedArchive&);

 private:
  FetchedArchive(FileSource,
                 std::map<std::string, std::string, std::less<void>>,
                 std::set<std::string, std::less<void>>,
                 std::optional<ReadableZip>);

  FileSource source_;
  std::map<std::string, std::string, std::less<void>> extracted_;
  std::set<std::string, std::less<void>> members_;
  std::optional<ReadableZip> zip_file_;
};

}  // namespace cuttlefish

namespace fmt {

template <>
struct formatter<::cuttlefish::FetchedArchive> : ostream_formatter {};

}  // namespace fmt
