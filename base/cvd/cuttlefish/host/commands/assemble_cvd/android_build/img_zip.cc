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

#include "cuttlefish/host/commands/assemble_cvd/android_build/img_zip.h"

#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <ostream>
#include <set>
#include <string>
#include <string_view>
#include <utility>

#include "absl/strings/str_cat.h"
#include "absl/strings/strip.h"
#include "fmt/ostream.h"
#include "google/protobuf/text_format.h"

#include "cuttlefish/common/libs/key_equals_value/key_equals_value.h"
#include "cuttlefish/common/libs/utils/result.h"
#include "cuttlefish/host/commands/assemble_cvd/android_build/android_build.h"
#include "cuttlefish/host/commands/assemble_cvd/android_build/find_build_archive.h"
#include "cuttlefish/host/commands/assemble_cvd/proto/guest_config.pb.h"
#include "cuttlefish/host/libs/config/build_archive.h"
#include "cuttlefish/host/libs/config/fetcher_config.h"
#include "cuttlefish/host/libs/config/file_source.h"

namespace cuttlefish {
namespace {

static constexpr std::string_view kImgMatch = "-img-";
static constexpr std::string_view kImgSuffix = ".img";

class ImgZipImpl : public AndroidBuild {
 public:
  static Result<std::unique_ptr<ImgZipImpl>> FromBuildArchive(
      BuildArchive archive) {
    std::unique_ptr<ImgZipImpl> img_zip(new ImgZipImpl(std::move(archive)));

    CF_EXPECT(img_zip->Images());

    return std::move(img_zip);
  }

  Result<std::set<std::string, std::less<void>>> Images() override {
    std::set<std::string, std::less<void>> partitions;
    for (std::string_view member : archive_.Members()) {
      if (!absl::ConsumeSuffix(&member, kImgSuffix)) {
        continue;
      }
      absl::ConsumePrefix(&member, "/");
      partitions.emplace(member);
    }
    return partitions;
  }

  Result<std::string> ImageFile(
      std::string_view name,
      std::optional<std::string_view> extract_dir) override {
    std::string member_name = absl::StrCat(name, kImgSuffix);
    return CF_EXPECT(archive_.MemberFilepath(member_name, extract_dir));
  }

  // TODO: schuffelen - put in AndroidBuild
  Result<std::map<std::string, std::string>> AndroidInfoTxt() {
    std::string contents =
        CF_EXPECT(archive_.MemberContents("android-info.txt"));
    return CF_EXPECT(ParseKeyEqualsValue(contents));
  }

  // TODO: schuffelen - put in AndroidBuild
  Result<config::GuestConfigFile> GuestConfigProto() {
    std::string contents =
        CF_EXPECT(archive_.MemberContents("cuttlefish-guest-config.txtpb"));

    config::GuestConfigFile proto_config;
    CF_EXPECT(
        google::protobuf::TextFormat::ParseFromString(contents, &proto_config));

    return proto_config;
  }

 private:
  ImgZipImpl(BuildArchive archive) : archive_(std::move(archive)) {}

  std::ostream& Format(std::ostream& out) const override {
    fmt::print(out, "ImgZip {{ {} }}", archive_);
    return out;
  }

  BuildArchive archive_;
};

}  // namespace

Result<std::unique_ptr<AndroidBuild>> ImgZip(const FetcherConfig& config,
                                             FileSource source) {
  BuildArchive archive = CF_EXPECT(FindBuildArchive(config, source, kImgMatch));
  return CF_EXPECT(ImgZipImpl::FromBuildArchive(std::move(archive)));
}

Result<std::unique_ptr<AndroidBuild>> ImgZip(const std::string& path) {
  BuildArchive archive = CF_EXPECT(FindBuildArchive(path, kImgMatch));
  return CF_EXPECT(ImgZipImpl::FromBuildArchive(std::move(archive)));
}

}  // namespace cuttlefish
