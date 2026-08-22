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

#include "cuttlefish/host/libs/web/gcs_build_api.h"

#include <stdint.h>

#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "absl/log/log.h"
#include "absl/strings/numbers.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "absl/strings/strip.h"
#include "fmt/format.h"
#include "fmt/ostream.h"
#include "json/value.h"

#include "cuttlefish/common/libs/utils/contains.h"
#include "cuttlefish/common/libs/utils/files.h"
#include "cuttlefish/host/libs/web/android_build.h"
#include "cuttlefish/host/libs/web/android_build_api.h"
#include "cuttlefish/host/libs/web/android_build_string.h"
#include "cuttlefish/host/libs/web/build_api_zip.h"
#include "cuttlefish/host/libs/web/credential_source.h"
#include "cuttlefish/host/libs/web/digest.h"
#include "cuttlefish/host/libs/web/http_client/http_client.h"
#include "cuttlefish/host/libs/web/http_client/http_json.h"
#include "cuttlefish/host/libs/web/http_client/scrub_secrets.h"
#include "cuttlefish/host/libs/web/http_client/url_escape.h"
#include "cuttlefish/host/libs/web/url_download.h"
#include "cuttlefish/host/libs/web/url_namespace.h"
#include "cuttlefish/host/libs/zip/libzip_cc/archive.h"
#include "cuttlefish/host/libs/zip/libzip_cc/seekable_source.h"
#include "cuttlefish/host/libs/zip/remote_zip.h"
#include "cuttlefish/host/libs/zip/zip_file.h"
#include "cuttlefish/result/result.h"

namespace cuttlefish {
namespace {

constexpr char kStorageApiUrl[] = "https://storage.googleapis.com/storage/v1";

// `cvd login` does not consent to the storage scope by default, so a rejected
// anonymous read has to name the command that does.
constexpr char kAnonymousHint[] =
    "\n\nThis fetch presented no credentials.  For a bucket that is not "
    "public, run `cvd login "
    "--scopes=https://www.googleapis.com/auth/devstorage.read_only`";

std::string ObjectUrl(const std::string& bucket, const std::string& object) {
  return fmt::format("{}/b/{}/o/{}", kStorageApiUrl, UrlEscape(bucket),
                     UrlEscape(object));
}

std::string MediaUrl(const std::string& bucket, const std::string& object) {
  return absl::StrCat(ObjectUrl(bucket, object), "?alt=media");
}

std::string ListUrl(const std::string& bucket, const std::string& prefix,
                    const std::string& page_token) {
  std::string url =
      fmt::format("{}/b/{}/o?delimiter=%2F&prefix={}", kStorageApiUrl,
                  UrlEscape(bucket), UrlEscape(prefix));
  if (!page_token.empty()) {
    absl::StrAppend(&url, "&pageToken=", UrlEscape(page_token));
  }
  return url;
}

std::string ArtifactNames(const GcsBuild& build) {
  return absl::StrJoin(build.contents, ", ",
                       [](std::string* out, const auto& entry) {
                         absl::StrAppend(out, entry.first);
                       });
}

Result<std::string> ObjectName(const GcsBuild& build,
                               const std::string& artifact_name) {
  if (build.object.has_value()) {
    CF_EXPECTF(artifact_name == *build.object,
               "The build '{}' holds only '{}', so it has no '{}'.", build.id,
               *build.object, artifact_name);
  } else {
    CF_EXPECTF(Contains(build.contents, artifact_name),
               "The build '{}' has no '{}'.  It holds [{}].", build.id,
               artifact_name, ArtifactNames(build));
  }
  return absl::StrCat(build.prefix, artifact_name);
}

std::optional<uint64_t> ParseSize(const std::string& size) {
  uint64_t parsed = 0;
  if (!absl::SimpleAtoi(size, &parsed)) {
    return std::nullopt;
  }
  return parsed;
}

// The size the listing or the metadata probe already reported, which spares
// the zip reader a round trip to ask for it.
std::optional<uint64_t> ArtifactSize(const GcsBuild& build,
                                     const std::string& artifact_name) {
  if (build.object.has_value()) {
    return build.size;
  }
  auto entry = build.contents.find(artifact_name);
  if (entry == build.contents.end()) {
    return std::nullopt;
  }
  return entry->second.size;
}

std::optional<std::string> ArtifactGeneration(
    const GcsBuild& build, const std::string& artifact_name) {
  if (build.object.has_value()) {
    return build.generation;
  }
  auto entry = build.contents.find(artifact_name);
  if (entry == build.contents.end() || entry->second.generation.empty()) {
    return std::nullopt;
  }
  return entry->second.generation;
}

// The media URL of one artifact, naming the generation wherever it is known so
// that every request against the URL reads one version of the object.
Result<std::string> ArtifactUrl(const GcsBuild& build,
                                const std::string& artifact_name) {
  std::string url =
      MediaUrl(build.bucket, CF_EXPECT(ObjectName(build, artifact_name)));
  if (std::optional<std::string> generation =
          ArtifactGeneration(build, artifact_name)) {
    absl::StrAppend(&url, "&generation=", *generation);
  }
  return url;
}

// The listing and the metadata probe both report an md5, so a `gs://` download
// is checked whether or not the build string carried a digest.
Result<void> VerifyArtifact(const GcsBuild& build,
                            const std::string& artifact_name,
                            const std::string& path) {
  if (build.object.has_value()) {
    if (build.sha256.has_value()) {
      CF_EXPECT(VerifySha256(path, *build.sha256, artifact_name));
    }
    if (build.md5.has_value()) {
      CF_EXPECT(VerifyMd5(path, *build.md5, artifact_name));
    }
    return {};
  }
  auto entry = build.contents.find(artifact_name);
  if (entry != build.contents.end() && !entry->second.md5.empty()) {
    CF_EXPECT(VerifyMd5(path, entry->second.md5, artifact_name));
  }
  return {};
}

Result<Json::Value> ResponseJson(const HttpResponse<Json::Value>& response,
                                 const std::string& url, bool authenticated) {
  VLOG(0) << "API response data:\n"
          << ScrubSecrets(response.data.toStyledString());
  std::string hint;
  if (!authenticated &&
      (response.http_code == 401 || response.http_code == 403)) {
    hint = kAnonymousHint;
  }
  CF_EXPECTF(response.HttpSuccess(),
             "Error response from Cloud Storage for '{}' - {}:{}\nCheck log "
             "file for full response{}",
             url, response.http_code, response.StatusDescription(), hint);
  return response.data;
}

}  // namespace

GcsBuildApi::GcsBuildApi(HttpClient& http_client,
                         CredentialSource* credential_source)
    : http_client_(http_client), credential_source_(credential_source) {}

Result<std::vector<std::string>> GcsBuildApi::Headers() {
  std::vector<std::string> headers;
  if (credential_source_ != nullptr) {
    headers.emplace_back("Authorization: Bearer " +
                         CF_EXPECT(credential_source_->Credential()));
  }
  return headers;
}

Result<Build> GcsBuildApi::GetBuild(const BuildString& build_string) {
  if (const auto* gcs = std::get_if<GcsBuildString>(&build_string)) {
    return CF_EXPECT(GetBuild(*gcs));
  }
  return CF_ERRF("GcsBuildApi cannot handle '{}'", fmt::streamed(build_string));
}

Result<Build> GcsBuildApi::GetBuild(const GcsBuildString& build_string) {
  GcsBuild build = CF_EXPECT(GcsBuild::FromBuildString(build_string));
  if (build.object.has_value()) {
    CF_EXPECT(ProbeObject(build));
  } else {
    CF_EXPECT(ListContents(build));
  }
  return build;
}

Result<void> GcsBuildApi::ListContents(GcsBuild& build) {
  std::string page_token;
  do {
    const std::string url = ListUrl(build.bucket, build.prefix, page_token);
    HttpResponse<Json::Value> response =
        CF_EXPECT(HttpGetToJson(http_client_, url, CF_EXPECT(Headers())));
    const Json::Value json = CF_EXPECT(
        ResponseJson(response, build.id, credential_source_ != nullptr));

    for (const Json::Value& item : json["items"]) {
      const std::string object = item["name"].asString();
      std::string_view name = object;
      // The prefix itself is listed when a placeholder object created it.
      if (!absl::ConsumePrefix(&name, build.prefix) || name.empty()) {
        continue;
      }
      build.contents.emplace(name,
                             GcsObjectInfo{
                                 .generation = item["generation"].asString(),
                                 .md5 = item["md5Hash"].asString(),
                                 .size = ParseSize(item["size"].asString()),
                             });
    }

    if (json.isMember("nextPageToken")) {
      page_token = json["nextPageToken"].asString();
    } else {
      page_token = "";
    }
  } while (!page_token.empty());

  CF_EXPECTF(!build.contents.empty(), "The build '{}' holds no artifacts.",
             build.id);
  return {};
}

Result<void> GcsBuildApi::ProbeObject(GcsBuild& build) {
  const std::string url =
      ObjectUrl(build.bucket, absl::StrCat(build.prefix, *build.object));
  HttpResponse<Json::Value> response =
      CF_EXPECT(HttpGetToJson(http_client_, url, CF_EXPECT(Headers())));
  const Json::Value json = CF_EXPECT(
      ResponseJson(response, build.id, credential_source_ != nullptr));

  if (json.isMember("generation")) {
    build.generation = json["generation"].asString();
  }
  if (json.isMember("md5Hash")) {
    build.md5 = json["md5Hash"].asString();
  }
  build.size = ParseSize(json["size"].asString());
  return {};
}

Result<std::string> GcsBuildApi::DownloadFile(
    const Build& build, const std::string& target_directory,
    const std::string& artifact_name) {
  if (const auto* gcs = std::get_if<GcsBuild>(&build)) {
    return CF_EXPECT(DownloadFile(*gcs, target_directory, artifact_name));
  }
  return CF_ERRF("GcsBuildApi cannot handle '{}'", FetchLabel(build));
}

Result<std::string> GcsBuildApi::DownloadFile(
    const GcsBuild& build, const std::string& target_directory,
    const std::string& artifact_name) {
  const std::string dest_path =
      ConstructTargetFilepath(target_directory, artifact_name);
  CF_EXPECT(EnsureDirectoryExists(target_directory));

  if (IsArchiveMember(build.object, build.filepath, artifact_name)) {
    ReadableZip zip = CF_EXPECT(OpenZip(*this, build, *build.object));
    CF_EXPECTF(ExtractFile(zip, artifact_name, dest_path),
               "Could not read '{}' out of '{}'.", artifact_name, build.id);
    return dest_path;
  }

  // A generation makes the bytes the URL serves fixed, which is what a resumed
  // download needs of it.
  UrlDownload download = {
      .url = CF_EXPECT(ArtifactUrl(build, artifact_name)),
      .headers = CF_EXPECT(Headers()),
      .resumable = ArtifactGeneration(build, artifact_name).has_value(),
      .size = ArtifactSize(build, artifact_name),
  };
  CF_EXPECTF(DownloadUrlToFile(http_client_, download, dest_path),
             "Could not download '{}' from '{}'", artifact_name, build.id);
  CF_EXPECT(VerifyArtifact(build, artifact_name, dest_path));
  return dest_path;
}

Result<SeekableZipSource> GcsBuildApi::FileReader(
    const Build& build, const std::string& artifact_name) {
  if (const auto* gcs = std::get_if<GcsBuild>(&build)) {
    return CF_EXPECT(FileReader(*gcs, artifact_name));
  }
  return CF_ERRF("GcsBuildApi cannot handle '{}'", FetchLabel(build));
}

Result<SeekableZipSource> GcsBuildApi::FileReader(
    const GcsBuild& build, const std::string& artifact_name) {
  const std::string url = CF_EXPECT(ArtifactUrl(build, artifact_name));
  std::vector<std::string> headers = CF_EXPECT(Headers());
  if (std::optional<uint64_t> size = ArtifactSize(build, artifact_name)) {
    return CF_EXPECT(
        ZipSourceFromUrl(http_client_, url, std::move(headers), *size));
  }
  return CF_EXPECT(ZipSourceFromUrl(http_client_, url, std::move(headers)));
}

}  // namespace cuttlefish
