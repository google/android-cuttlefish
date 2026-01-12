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

#include "cuttlefish/host/commands/assemble_cvd/android_build/target_files.h"

#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <ostream>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/strings/str_cat.h"
#include "absl/strings/str_split.h"
#include "absl/strings/strip.h"
#include "cuttlefish/host/commands/assemble_cvd/android_build/combined_android_build.h"
#include "fmt/ostream.h"

#include "cuttlefish/common/libs/key_equals_value/key_equals_value.h"
#include "cuttlefish/host/commands/assemble_cvd/android_build/android_build.h"
#include "cuttlefish/host/commands/assemble_cvd/android_build/find_build_archive.h"
#include "cuttlefish/host/commands/assemble_cvd/android_build/misc_info_metadata.h"
#include "cuttlefish/host/libs/config/build_archive.h"
#include "cuttlefish/host/libs/config/fetcher_config.h"
#include "cuttlefish/host/libs/config/file_source.h"
#include "cuttlefish/result/result.h"

namespace cuttlefish {
namespace {

static constexpr std::string_view kTargetFilesMatch = "-target_files-";
static constexpr std::string_view kImgSuffix = ".img";

class TargetFilesImpl : public AndroidBuild {
 public:
  static Result<std::unique_ptr<TargetFilesImpl>> FromBuildArchive(
      BuildArchive archive) {
    TargetFilesImpl target_files(std::move(archive));

    CF_EXPECT(!target_files.archive_.Members().empty());

    return std::make_unique<TargetFilesImpl>(std::move(target_files));
  }

  Result<std::map<std::string, std::string, std::less<void>>> MiscInfo() {
    static constexpr std::string_view kMiscInfoTxt = "META/misc_info.txt";
    std::string contents = CF_EXPECT(archive_.MemberContents(kMiscInfoTxt));
    return CF_EXPECT(ParseKeyEqualsValue(contents));
  }

  // Image files are stored as `IMAGES/*.img` archive members.
  Result<std::set<std::string, std::less<void>>> Images() override {
    std::set<std::string, std::less<void>> partitions;
    for (std::string_view member : archive_.Members()) {
      if (!absl::ConsumeSuffix(&member, kImgSuffix)) {
        continue;
      }
      absl::ConsumePrefix(&member, "/");
      if (!absl::ConsumePrefix(&member, "IMAGES/")) {
        continue;
      }
      partitions.emplace(member);
    }
    return partitions;
  }

  Result<std::string> ImageFile(std::string_view name, bool extract) override {
    std::string member_name = absl::StrCat(name, kImgSuffix);
    if (extract) {
      return CF_EXPECT(archive_.MemberFilepath(member_name, std::nullopt));
    } else {
      return CF_EXPECT(archive_.MemberFilepath(member_name, extract_dir_));
    }
  }

  Result<void> SetExtractDir(std::string_view dir) override {
    extract_dir_ = dir;
    return {};
  }

  Result<std::set<std::string, std::less<void>>> AbPartitions() override {
    static constexpr std::string_view kAbTxt = "META/ab_partitions.txt";
    std::string contents = CF_EXPECT(archive_.MemberContents(kAbTxt));
    return absl::StrSplit(contents, "\n", absl::SkipEmpty());
  }

 private:
  TargetFilesImpl(BuildArchive archive) : archive_(std::move(archive)) {}

  // The `META/ab_partitions.txt` archive member has one entry per line.

  std::ostream& Format(std::ostream& out) const override {
    fmt::print(out, "TargetFiles {{ {} }}", archive_);
    return out;
  }

  BuildArchive archive_;
  std::optional<std::string> extract_dir_;
};

Result<std::unique_ptr<AndroidBuild>> TargetFiles(BuildArchive archive) {
  std::unique_ptr<TargetFilesImpl> target =
      CF_EXPECT(TargetFilesImpl::FromBuildArchive(std::move(archive)));

  std::map<std::string, std::string, std::less<void>> misc_info =
      CF_EXPECT(target->MiscInfo());

  std::unique_ptr<AndroidBuild> misc_info_build =
      CF_EXPECT(AndroidBuildFromMiscInfo(std::move(misc_info)));

  std::vector<std::unique_ptr<AndroidBuild>> builds;
  builds.emplace_back(std::move(target));
  builds.emplace_back(std::move(misc_info_build));
  return CF_EXPECT(CombinedAndroidBuild("TargetFiles", std::move(builds)));
}

}  // namespace

Result<std::unique_ptr<AndroidBuild>> TargetFiles(const FetcherConfig& config,
                                                  FileSource source) {
  BuildArchive archive =
      CF_EXPECT(FindBuildArchive(config, source, kTargetFilesMatch));
  return CF_EXPECT(TargetFiles(std::move(archive)));
}

Result<std::unique_ptr<AndroidBuild>> TargetFiles(const std::string& path) {
  BuildArchive archive = CF_EXPECT(FindBuildArchive(path, kTargetFilesMatch));
  return CF_EXPECT(TargetFiles(std::move(archive)));
}

}  // namespace cuttlefish
