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

#include "cuttlefish/host/libs/web/http_build_api.h"

#include <stddef.h>
#include <stdint.h>

#include <optional>
#include <string>
#include <string_view>
#include <variant>

#include "absl/strings/match.h"
#include "absl/strings/numbers.h"
#include "absl/strings/str_cat.h"
#include "fmt/ostream.h"

#include "cuttlefish/common/libs/utils/files.h"
#include "cuttlefish/host/libs/web/android_build.h"
#include "cuttlefish/host/libs/web/android_build_api.h"
#include "cuttlefish/host/libs/web/android_build_string.h"
#include "cuttlefish/host/libs/web/build_api_zip.h"
#include "cuttlefish/host/libs/web/http_client/http_client.h"
#include "cuttlefish/host/libs/web/http_client/http_file.h"
#include "cuttlefish/host/libs/zip/libzip_cc/archive.h"
#include "cuttlefish/host/libs/zip/libzip_cc/seekable_source.h"
#include "cuttlefish/host/libs/zip/remote_zip.h"
#include "cuttlefish/host/libs/zip/zip_file.h"
#include "cuttlefish/result/result.h"

namespace cuttlefish {
namespace {

constexpr long kPartialContent = 206;

// The object form names one archive, so `{selector}` names a member of it
// rather than a second artifact of the build.
bool IsArchiveMember(const HttpBuild& build, const std::string& artifact_name) {
  return build.object.has_value() && artifact_name != *build.object &&
         absl::EndsWith(*build.object, ".zip") &&
         build.filepath == artifact_name;
}

Result<std::string> ArtifactUrl(const HttpBuild& build,
                                const std::string& artifact_name) {
  if (build.object.has_value()) {
    CF_EXPECTF(artifact_name == *build.object,
               "The build '{}' holds only '{}', so it has no '{}'.", build.id,
               *build.object, artifact_name);
    return build.url;
  }
  return absl::StrCat(build.base, artifact_name);
}

bool ServesRanges(const HttpResponse<void>& response) {
  if (response.http_code == kPartialContent) {
    return true;
  }
  std::optional<std::string_view> ranges =
      HeaderValue(response.headers, "accept-ranges");
  return ranges.has_value() && absl::StrContains(*ranges, "bytes");
}

// The whole object's length: the total of a partial response's `Content-Range`
// or, where the origin answered the range request with the whole object, its
// `Content-Length`.
std::optional<uint64_t> ProbedSize(const HttpResponse<void>& response) {
  uint64_t size = 0;
  if (std::optional<std::string_view> range =
          HeaderValue(response.headers, "content-range")) {
    size_t total = range->rfind('/');
    if (total != std::string_view::npos &&
        absl::SimpleAtoi(range->substr(total + 1), &size)) {
      return size;
    }
  }
  std::optional<std::string_view> length =
      HeaderValue(response.headers, "content-length");
  if (response.http_code != kPartialContent && length.has_value() &&
      absl::SimpleAtoi(*length, &size)) {
    return size;
  }
  return std::nullopt;
}

}  // namespace

HttpBuildApi::HttpBuildApi(HttpClient& http_client)
    : http_client_(http_client) {}

Result<Build> HttpBuildApi::GetBuild(const BuildString& build_string) {
  if (const auto* http = std::get_if<HttpBuildString>(&build_string)) {
    return CF_EXPECT(GetBuild(*http));
  }
  return CF_ERRF("HttpBuildApi cannot handle '{}'",
                 fmt::streamed(build_string));
}

Result<Build> HttpBuildApi::GetBuild(const HttpBuildString& build_string) {
  HttpBuild build = CF_EXPECT(HttpBuild::FromBuildString(build_string));
  // A directory of plain HTTPS URLs has nothing to list and nothing to probe,
  // so its artifacts are only known to be absent when they answer 404.
  if (build.object.has_value()) {
    CF_EXPECT(ProbeObject(build));
  }
  return build;
}

Result<void> HttpBuildApi::ProbeObject(HttpBuild& build) {
  // A pre-signed URL signs the verb, so this is a GET of one byte rather than
  // the HEAD the size alone would call for.
  HttpRequest request = {
      .method = HttpMethod::kGet,
      .url = build.url,
      .headers = {"Range: bytes=0-0"},
  };
  auto discard = [](char*, size_t) { return true; };
  HttpResponse<void> response =
      CF_EXPECT(http_client_.DownloadToCallback(request, discard));

  CF_EXPECTF(!response.HttpRedirect(),
             "'{}' redirects with {}, and redirects are not followed.  Name "
             "the URL it redirects to.",
             build.id, response.http_code);
  CF_EXPECTF(response.HttpSuccess(), "'{}' is missing or inaccessible - {}:{}",
             build.id, response.http_code, response.StatusDescription());

  if (std::optional<std::string_view> etag =
          HeaderValue(response.headers, "etag")) {
    build.etag = std::string(*etag);
  }
  build.accept_ranges = ServesRanges(response);
  build.size = ProbedSize(response);
  return {};
}

Result<std::string> HttpBuildApi::DownloadFile(
    const Build& build, const std::string& target_directory,
    const std::string& artifact_name) {
  if (const auto* http = std::get_if<HttpBuild>(&build)) {
    return CF_EXPECT(DownloadFile(*http, target_directory, artifact_name));
  }
  return CF_ERRF("HttpBuildApi cannot handle '{}'", FetchLabel(build));
}

Result<std::string> HttpBuildApi::DownloadFile(
    const HttpBuild& build, const std::string& target_directory,
    const std::string& artifact_name) {
  const std::string dest_path =
      ConstructTargetFilepath(target_directory, artifact_name);
  CF_EXPECT(EnsureDirectoryExists(target_directory));

  if (IsArchiveMember(build, artifact_name)) {
    if (!build.accept_ranges) {
      return CF_ERRF(
          "'{}' does not serve range requests, so '{}' cannot be read out of "
          "it.",
          build.id, artifact_name);
    }
    ReadableZip zip = CF_EXPECT(OpenZip(*this, build, *build.object));
    CF_EXPECTF(ExtractFile(zip, artifact_name, dest_path),
               "Could not read '{}' out of '{}'.", artifact_name, build.id);
    return dest_path;
  }

  const std::string url = CF_EXPECT(ArtifactUrl(build, artifact_name));
  HttpResponse<std::string> response =
      CF_EXPECT(HttpGetToFile(http_client_, url, dest_path));
  CF_EXPECTF(response.HttpSuccess(),
             "Could not download '{}' from '{}' - {}:{}", artifact_name,
             build.id, response.http_code, response.StatusDescription());
  return dest_path;
}

Result<SeekableZipSource> HttpBuildApi::FileReader(
    const Build& build, const std::string& artifact_name) {
  if (const auto* http = std::get_if<HttpBuild>(&build)) {
    return CF_EXPECT(FileReader(*http, artifact_name));
  }
  return CF_ERRF("HttpBuildApi cannot handle '{}'", FetchLabel(build));
}

Result<SeekableZipSource> HttpBuildApi::FileReader(
    const HttpBuild& build, const std::string& artifact_name) {
  const std::string url = CF_EXPECT(ArtifactUrl(build, artifact_name));
  if (build.size.has_value()) {
    return CF_EXPECT(ZipSourceFromUrl(http_client_, url, {}, *build.size));
  }
  // Only a directory reaches here, having had no probe to learn a size from.
  return CF_EXPECT(ZipSourceFromUrl(http_client_, url, {}));
}

}  // namespace cuttlefish
