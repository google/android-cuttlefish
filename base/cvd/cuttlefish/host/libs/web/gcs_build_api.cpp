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

#include <map>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/log/log.h"
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "absl/strings/strip.h"
#include "fmt/format.h"
#include "json/value.h"

#include "cuttlefish/common/libs/utils/contains.h"
#include "cuttlefish/common/libs/utils/files.h"
#include "cuttlefish/common/libs/utils/json.h"
#include "cuttlefish/host/libs/web/android_build.h"
#include "cuttlefish/host/libs/web/android_build_string.h"
#include "cuttlefish/host/libs/web/build_api_zip.h"
#include "cuttlefish/host/libs/web/credential_source.h"
#include "cuttlefish/host/libs/web/http_client/http_client.h"
#include "cuttlefish/host/libs/web/http_client/http_file.h"
#include "cuttlefish/host/libs/web/http_client/http_json.h"
#include "cuttlefish/host/libs/web/http_client/url_escape.h"
#include "cuttlefish/host/libs/zip/libzip_cc/archive.h"
#include "cuttlefish/host/libs/zip/libzip_cc/seekable_source.h"
#include "cuttlefish/host/libs/zip/remote_zip.h"
#include "cuttlefish/host/libs/zip/zip_file.h"
#include "cuttlefish/result/result.h"

namespace cuttlefish {
namespace {

constexpr char kStorageApiUrl[] = "https://storage.googleapis.com/storage/v1";
constexpr long kUnauthorized = 401;
constexpr long kForbidden = 403;

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
  return fmt::format("{}?alt=media", ObjectUrl(bucket, object));
}

std::string ListUrl(const std::string& bucket, const std::string& prefix,
                    const std::string& page_token) {
  std::string url =
      fmt::format("{}/b/{}/o?delimiter=%2F&prefix={}", kStorageApiUrl,
                  UrlEscape(bucket), UrlEscape(prefix));
  if (!page_token.empty()) {
    url += fmt::format("&pageToken={}", UrlEscape(page_token));
  }
  return url;
}

std::string ArtifactNames(const GcsBuild& build) {
  return absl::StrJoin(build.contents, ", ",
                       [](std::string* out, const auto& entry) {
                         absl::StrAppend(out, entry.first);
                       });
}

// The object form names one archive, so `{selector}` names a member of it
// rather than a second artifact of the build.
bool IsArchiveMember(const GcsBuild& build, const std::string& artifact_name) {
  return build.object.has_value() && artifact_name != *build.object &&
         absl::EndsWith(*build.object, ".zip") &&
         build.filepath == artifact_name;
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
  return build.prefix + artifact_name;
}

Result<Json::Value> ResponseJson(const HttpResponse<Json::Value>& response,
                                 const GcsBuild& build, bool authenticated) {
  std::string_view hint;
  if (!authenticated && (response.http_code == kUnauthorized ||
                         response.http_code == kForbidden)) {
    hint = kAnonymousHint;
  }
  const std::string source = fmt::format("Cloud Storage for '{}'", build.id);
  return JsonFromResponse(response, JsonResponseOptions{
                                        .source = source,
                                        .hint = hint,
                                    });
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

Result<GcsBuild> GcsBuildApi::GetBuild(const GcsBuildString& build_string) {
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
    const HttpResponse<Json::Value> response =
        CF_EXPECT(HttpGetToJson(http_client_, url, CF_EXPECT(Headers())));
    const Json::Value json =
        CF_EXPECT(ResponseJson(response, build, credential_source_ != nullptr));

    for (const Json::Value& item : json["items"]) {
      const std::string object =
          CF_EXPECT(GetValue<std::string>(item, {"name"}));
      std::string_view name = object;
      // The prefix itself is listed when a placeholder object created it.
      if (!absl::ConsumePrefix(&name, build.prefix) || name.empty()) {
        continue;
      }
      GcsObjectInfo info;
      if (item.isMember("generation")) {
        info.generation = item["generation"].asString();
      }
      if (item.isMember("md5Hash")) {
        info.md5 = item["md5Hash"].asString();
      }
      build.contents.emplace(name, std::move(info));
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
  const std::string url = ObjectUrl(build.bucket, build.prefix + *build.object);
  const HttpResponse<Json::Value> response =
      CF_EXPECT(HttpGetToJson(http_client_, url, CF_EXPECT(Headers())));
  const Json::Value json =
      CF_EXPECT(ResponseJson(response, build, credential_source_ != nullptr));

  if (json.isMember("generation")) {
    build.generation = json["generation"].asString();
  }
  if (json.isMember("md5Hash")) {
    build.md5 = json["md5Hash"].asString();
  }
  VLOG(1) << build.id << " is " << json["size"].asString() << " bytes";
  return {};
}

Result<std::string> GcsBuildApi::DownloadFile(
    const GcsBuild& build, const std::string& target_directory,
    const std::string& artifact_name) {
  const std::string dest_path =
      ConstructTargetFilepath(target_directory, artifact_name);
  CF_EXPECT(EnsureDirectoryExists(target_directory));

  if (IsArchiveMember(build, artifact_name)) {
    SeekableZipSource source = CF_EXPECT(FileReader(build, *build.object));
    ReadableZip zip = CF_EXPECT(OpenZip(std::move(source)));
    CF_EXPECTF(ExtractFile(zip, artifact_name, dest_path),
               "Could not read '{}' out of '{}'.", artifact_name, build.id);
    return dest_path;
  }

  const std::string url =
      MediaUrl(build.bucket, CF_EXPECT(ObjectName(build, artifact_name)));
  HttpResponse<std::string> response = CF_EXPECT(
      HttpGetToFile(http_client_, url, dest_path, CF_EXPECT(Headers())));
  CF_EXPECTF(response.HttpSuccess(),
             "Could not download '{}' from '{}' - {}:{}", artifact_name,
             build.id, response.http_code, response.StatusDescription());
  return dest_path;
}

Result<SeekableZipSource> GcsBuildApi::FileReader(
    const GcsBuild& build, const std::string& artifact_name) {
  const std::string url =
      MediaUrl(build.bucket, CF_EXPECT(ObjectName(build, artifact_name)));
  return CF_EXPECT(ZipSourceFromUrl(http_client_, url, CF_EXPECT(Headers())));
}

}  // namespace cuttlefish
