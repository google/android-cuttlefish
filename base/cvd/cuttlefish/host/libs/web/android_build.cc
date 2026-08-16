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

#include "cuttlefish/host/libs/web/android_build.h"

#include <stddef.h>

#include <optional>
#include <ostream>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "absl/strings/str_join.h"
#include "fmt/format.h"

#include "cuttlefish/common/libs/utils/environment.h"
#include "cuttlefish/host/libs/web/android_build_string.h"
#include "cuttlefish/host/libs/web/http_client/scrub_secrets.h"
#include "cuttlefish/host/libs/web/url_namespace.h"
#include "cuttlefish/result/result.h"

namespace cuttlefish {
namespace {

constexpr char kUrlTarget[] = "url";

}  // namespace

std::ostream& operator<<(std::ostream& out, const DeviceBuild& build) {
  return out << "(id=\"" << build.id << "\", branch=\"" << build.branch
             << "\", target=\"" << build.target << "\", filepath=\""
             << build.filepath.value_or("") << "\")";
}

DirectoryBuild::DirectoryBuild(std::vector<std::string> paths,
                               std::string target,
                               std::optional<std::string> filepath)
    : paths(std::move(paths)),
      target(std::move(target)),
      // TODO(schuffelen): Support local builds other than "eng"
      id("eng"),
      filepath(std::move(filepath)) {
  product = StringFromEnv("TARGET_PRODUCT", "");
}

std::ostream& operator<<(std::ostream& out, const DirectoryBuild& build) {
  auto paths = absl::StrJoin(build.paths, ":");
  return out << "(paths=\"" << paths << "\", target=\"" << build.target
             << "\", filepath=\"" << build.filepath.value_or("") << "\")";
}

Result<GcsBuild> GcsBuild::FromBuildString(const GcsBuildString& build_string) {
  ParsedUrl url = CF_EXPECT(ParseUrl(build_string.url));
  size_t basename = url.path.rfind('/') + 1;
  std::optional<std::string> object;
  if (!url.directory_form) {
    object = url.path.substr(basename);
  }
  return GcsBuild{
      .bucket = url.authority,
      .prefix = url.path.substr(0, basename),
      .object = object,
      .sha256 = build_string.sha256,
      .id = ScrubUrl(build_string.url),
      .target = kUrlTarget,
      .product = DeriveProduct(object.value_or("")),
      .filepath = build_string.filepath,
  };
}

std::ostream& operator<<(std::ostream& out, const GcsBuild& build) {
  return out << "(url=\"" << build.id << "\", filepath=\""
             << build.filepath.value_or("") << "\")";
}

Result<HttpBuild> HttpBuild::FromBuildString(
    const HttpBuildString& build_string) {
  ParsedUrl url = CF_EXPECT(ParseUrl(build_string.url));
  std::optional<std::string> object;
  if (!url.directory_form) {
    object = url.path.substr(url.path.rfind('/') + 1);
  }
  return HttpBuild{
      .url = url.directory_form ? "" : build_string.url,
      .base = url.directory_form ? build_string.url : "",
      .object = object,
      .sha256 = build_string.sha256,
      .id = ScrubUrl(build_string.url),
      .target = kUrlTarget,
      .product = DeriveProduct(object.value_or("")),
      .filepath = build_string.filepath,
  };
}

std::ostream& operator<<(std::ostream& out, const HttpBuild& build) {
  return out << "(url=\"" << build.id << "\", filepath=\""
             << build.filepath.value_or("") << "\")";
}

std::ostream& operator<<(std::ostream& out, const Build& build) {
  std::visit([&out](auto&& arg) { out << arg; }, build);
  return out;
}

std::string FetchLabel(const Build& build) {
  // return std::visit((auto&& arg) { return FetchLabel(arg); }, build)
  if (const GcsBuild* gcs = std::get_if<GcsBuild>(&build)) {
    return gcs->id;
  }
  if (const HttpBuild* http = std::get_if<HttpBuild>(&build)) {
    return http->id;
  }
  return fmt::format("{}/{}",
                     std::visit([](auto&& arg) { return arg.id; }, build),
                     std::visit([](auto&& arg) { return arg.target; }, build));
}

}  // namespace cuttlefish
