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

#include <map>
#include <optional>
#include <ostream>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <variant>
#include <vector>

#include "absl/strings/ascii.h"
#include "absl/strings/str_join.h"
#include "fmt/format.h"

#include "cuttlefish/common/libs/utils/contains.h"
#include "cuttlefish/common/libs/utils/environment.h"
#include "cuttlefish/host/libs/web/android_build_string.h"
#include "cuttlefish/host/libs/web/digest.h"
#include "cuttlefish/host/libs/web/http_client/scrub_secrets.h"
#include "cuttlefish/host/libs/web/url_namespace.h"
#include "cuttlefish/result/result.h"

namespace cuttlefish {
namespace {

// URL builds have no Android Build target, product or branch, so they carry
// this name wherever one is expected.
constexpr char kUrlName[] = "url";
constexpr size_t kUrlKeyLength = 12;

// Cache keys are directory names and an ETag may hold anything, so a version
// that is not already path safe stands in as a digest of itself.
std::string PathSafeVersion(const std::string& version) {
  for (char character : version) {
    if (!absl::ascii_isalnum(character) && character != '.' &&
        character != '_' && character != '-') {
      return Sha256Hex(version).substr(0, kUrlKeyLength);
    }
  }
  return version;
}

std::optional<std::string> UrlCacheKey(
    const std::string& id, const std::optional<std::string>& version) {
  if (!version.has_value()) {
    return std::nullopt;
  }
  return fmt::format("{}/{}/{}", kUrlName,
                     Sha256Hex(id).substr(0, kUrlKeyLength),
                     PathSafeVersion(*version));
}

// Returns where the name after the last '/' of `path` begins. A parsed path
// has no leading '/', so a name at the root is the whole path.
size_t BasenameStart(std::string_view path) {
  const size_t slash = path.rfind('/');
  return slash == std::string_view::npos ? 0 : slash + 1;
}

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
  const ParsedUrl url = CF_EXPECT(ParseUrl(build_string.url));
  const size_t basename = BasenameStart(url.path);
  std::optional<std::string> object;
  if (!url.IsDirectoryForm()) {
    object = url.path.substr(basename);
  }
  return GcsBuild{
      .bucket = url.authority,
      .prefix = url.path.substr(0, basename),
      .object = object,
      .sha256 = build_string.sha256,
      .id = ScrubUrl(build_string.url),
      .target = kUrlName,
      .product = DeriveProduct(object.value_or("")).value_or(kUrlName),
      .filepath = build_string.filepath,
  };
}

std::ostream& operator<<(std::ostream& out, const GcsBuild& build) {
  return out << "(url=\"" << build.id << "\", filepath=\""
             << build.filepath.value_or("") << "\")";
}

std::vector<std::string> GcsArtifactNames(const GcsBuild& build) {
  std::vector<std::string> names;
  names.reserve(build.contents.size());
  for (const auto& entry : build.contents) {
    names.push_back(entry.first);
  }
  return names;
}

Result<HttpBuild> HttpBuild::FromBuildString(
    const HttpBuildString& build_string) {
  const ParsedUrl url = CF_EXPECT(ParseUrl(build_string.url));
  std::optional<std::string> object;
  if (!url.IsDirectoryForm()) {
    object = url.path.substr(BasenameStart(url.path));
  }
  return HttpBuild{
      .url = build_string.url,
      .object = object,
      .sha256 = build_string.sha256,
      .id = ScrubUrl(build_string.url),
      .target = kUrlName,
      .product = DeriveProduct(object.value_or("")).value_or(kUrlName),
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

std::optional<std::string> ArtifactSha256(const Build& build,
                                          const std::string& artifact_name) {
  if (const GcsBuild* gcs = std::get_if<GcsBuild>(&build)) {
    return gcs->object == artifact_name ? gcs->sha256 : std::nullopt;
  }
  if (const HttpBuild* http = std::get_if<HttpBuild>(&build)) {
    return http->object == artifact_name ? http->sha256 : std::nullopt;
  }
  return std::nullopt;
}

bool BuildHasArtifact(const Build& build, const std::string& artifact_name) {
  const GcsBuild* gcs = std::get_if<GcsBuild>(&build);
  if (gcs == nullptr || gcs->object.has_value()) {
    return true;
  }
  return Contains(gcs->contents, artifact_name);
}

std::optional<std::string> BuildCacheKey(const Build& build,
                                         const std::string& artifact_name) {
  if (const GcsBuild* gcs = std::get_if<GcsBuild>(&build)) {
    if (gcs->object.has_value()) {
      return UrlCacheKey(
          gcs->id, gcs->generation.has_value() ? gcs->generation : gcs->sha256);
    }
    const std::map<std::string, GcsObjectInfo>::const_iterator entry =
        gcs->contents.find(artifact_name);
    if (entry == gcs->contents.end()) {
      return std::nullopt;
    }
    return UrlCacheKey(gcs->id, entry->second.generation);
  }
  if (const HttpBuild* http = std::get_if<HttpBuild>(&build)) {
    // A directory of plain HTTPS URLs reports nothing about its artifacts.
    if (!http->object.has_value()) {
      return std::nullopt;
    }
    return UrlCacheKey(http->id,
                       http->etag.has_value() ? http->etag : http->sha256);
  }
  return fmt::format("{}/{}",
                     std::visit([](auto&& arg) { return arg.id; }, build),
                     std::visit([](auto&& arg) { return arg.target; }, build));
}

std::tuple<std::string, std::string> GetBuildIdAndTarget(const Build& build) {
  std::string id = std::visit([](auto&& arg) { return arg.id; }, build);
  std::string target = std::visit([](auto&& arg) { return arg.target; }, build);
  return {id, target};
}

std::optional<std::string> GetFilepath(const Build& build) {
  return std::visit([](auto&& arg) { return arg.filepath; }, build);
}

std::string ConstructTargetFilepath(const std::string& directory,
                                    const std::string& filename) {
  return directory + "/" + filename;
}

}  // namespace cuttlefish
