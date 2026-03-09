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

#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <android-base/strings.h>
#include <fmt/core.h>

#include "cuttlefish/host/libs/web/android_build.h"
#include "cuttlefish/host/libs/web/android_build_string.h"
#include "cuttlefish/host/libs/web/credential_source.h"
#include "cuttlefish/host/libs/web/http_client/http_client.h"
#include "cuttlefish/host/libs/web/http_client/http_file.h"
#include "cuttlefish/host/libs/web/http_client/url_escape.h"
#include "cuttlefish/host/libs/zip/libzip_cc/seekable_source.h"
#include "cuttlefish/host/libs/zip/remote_zip.h"
#include "cuttlefish/result/result.h"

namespace cuttlefish {

GcsBuildApi::GcsBuildApi(HttpClient& http_client,
                         CredentialSource& credential_source)
    : http_client_(http_client), credential_source_(credential_source) {}

Result<std::vector<std::string>> GcsBuildApi::Headers() {
  std::vector<std::string> headers;
  headers.emplace_back("Authorization: Bearer " +
                       CF_EXPECT(credential_source_.Credential()));
  return headers;
}

// https://cloud.google.com/storage/docs/json_api/v1/objects/get
std::string GcsBuildApi::BuildGcsApiUrl(const std::string& bucket,
                                        const std::string& object) {
  return fmt::format(
      "https://storage.googleapis.com/storage/v1/b/{}/o/{}?alt=media",
      UrlEscape(bucket), UrlEscape(object));
}

Result<Build> GcsBuildApi::GetBuild(const GcsBuildString& build_string) {
  std::string_view url_view = build_string.url;
  CF_EXPECT(android::base::ConsumePrefix(&url_view, "gs://"),
            "GCS URL must start with gs://: " << build_string.url);
  auto parts = android::base::Split(std::string(url_view), "/");
  CF_EXPECT(!parts.empty(), "Invalid GCS URL: " << build_string.url);
  std::string bucket = parts[0];
  parts.erase(parts.begin());
  std::string object = android::base::Join(parts, "/");
  CF_EXPECT(!object.empty(),
            "Invalid GCS URL (missing object path): " << build_string.url);
  return GcsBuild{
      .bucket = std::move(bucket),
      .object = std::move(object),
      .filepath = build_string.filepath,
  };
}

Result<std::string> GcsBuildApi::DownloadFile(
    const GcsBuild& build, const std::string& target_directory,
    const std::string& artifact_name) {
  std::string api_url = BuildGcsApiUrl(build.bucket, build.object);
  std::string dest_path = target_directory + "/" + artifact_name;
  auto headers = CF_EXPECT(Headers());
  auto response =
      CF_EXPECT(HttpGetToFile(http_client_, api_url, dest_path, headers));
  CF_EXPECT(response.HttpSuccess(),
            "Failed to download gs://"
                << build.bucket << "/" << build.object
                << ", HTTP status: " << response.http_code << " ("
                << response.StatusDescription() << ")");
  return dest_path;
}

Result<SeekableZipSource> GcsBuildApi::FileReader(const GcsBuild& build,
                                                  const std::string&) {
  std::string api_url = BuildGcsApiUrl(build.bucket, build.object);
  auto headers = CF_EXPECT(Headers());
  return CF_EXPECT(ZipSourceFromUrl(http_client_, api_url, headers));
}

}  // namespace cuttlefish
