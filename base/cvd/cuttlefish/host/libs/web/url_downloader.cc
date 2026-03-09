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

#include "cuttlefish/host/libs/web/url_downloader.h"

#include <string>
#include <string_view>
#include <vector>

#include <android-base/strings.h>
#include <fmt/core.h>
#include "absl/log/log.h"

#include "cuttlefish/host/libs/web/http_client/http_client.h"
#include "cuttlefish/host/libs/web/http_client/http_file.h"
#include "cuttlefish/host/libs/web/http_client/url_escape.h"
#include "cuttlefish/result/result.h"

namespace cuttlefish {

Result<UrlScheme> ParseUrlScheme(const std::string& url) {
  if (android::base::StartsWith(url, "gs://")) {
    return UrlScheme::kGcs;
  }
  if (android::base::StartsWith(url, "https://")) {
    return UrlScheme::kHttps;
  }
  if (android::base::StartsWith(url, "http://")) {
    return UrlScheme::kHttp;
  }
  return CF_ERR("Unsupported URL scheme. Expected gs://, https://, or http://. "
                "Got: "
                << url);
}

std::string FilenameFromUrl(const std::string& url) {
  auto pos = url.rfind('/');
  if (pos == std::string::npos) {
    return url;
  }
  std::string filename = url.substr(pos + 1);
  // Remove query parameters if present
  auto query_pos = filename.find('?');
  if (query_pos != std::string::npos) {
    filename = filename.substr(0, query_pos);
  }
  return filename;
}

UrlDownloader::UrlDownloader(HttpClient& http_client,
                             CredentialSource* credential_source)
    : http_client_(http_client), credential_source_(credential_source) {}

Result<std::vector<std::string>> UrlDownloader::AuthHeaders() {
  std::vector<std::string> headers;
  if (credential_source_) {
    std::string credential = CF_EXPECT(credential_source_->Credential());
    headers.emplace_back("Authorization: Bearer " + credential);
  }
  return headers;
}

Result<std::string> UrlDownloader::Download(
    const std::string& url, const std::string& target_directory) {
  std::string filename = FilenameFromUrl(url);
  CF_EXPECT(!filename.empty(), "Could not extract filename from URL: " << url);

  std::string dest_path = target_directory + "/" + filename;
  LOG(INFO) << "Downloading " << url << " to " << dest_path;

  CF_EXPECT(DownloadToFile(url, dest_path));

  LOG(INFO) << "Successfully downloaded " << url;
  return dest_path;
}

Result<std::vector<std::string>> UrlDownloader::DownloadAll(
    const std::vector<std::string>& urls,
    const std::string& target_directory) {
  std::vector<std::string> downloaded_files;
  downloaded_files.reserve(urls.size());

  for (const auto& url : urls) {
    auto path = CF_EXPECT(Download(url, target_directory));
    downloaded_files.push_back(std::move(path));
  }

  return downloaded_files;
}

Result<void> UrlDownloader::DownloadToFile(const std::string& url,
                                           const std::string& dest_path) {
  UrlScheme scheme = CF_EXPECT(ParseUrlScheme(url));

  switch (scheme) {
    case UrlScheme::kGcs: {
      std::string_view url_view = url;
      CF_EXPECT(android::base::ConsumePrefix(&url_view, "gs://"));
      auto parts = android::base::Split(std::string(url_view), "/");
      CF_EXPECT(!parts.empty(), "Invalid GCS URL: " << url);
      std::string bucket = parts[0];
      parts.erase(parts.begin());
      std::string object = android::base::Join(parts, "/");
      CF_EXPECT(!object.empty(), "Invalid GCS URL (no object path): " << url);
      return DownloadFromGcs(bucket, object, dest_path);
    }

    case UrlScheme::kHttps:
    case UrlScheme::kHttp:
      return DownloadFromHttp(url, dest_path);
  }

  return CF_ERR("Unknown URL scheme");
}

Result<void> UrlDownloader::DownloadFromGcs(const std::string& bucket,
                                            const std::string& object,
                                            const std::string& dest_path) {
  // Google Cloud Storage JSON API
  // https://cloud.google.com/storage/docs/json_api/v1/objects/get
  std::string api_url = fmt::format(
      "https://storage.googleapis.com/storage/v1/b/{}/o/{}?alt=media",
      UrlEscape(bucket), UrlEscape(object));

  auto headers = CF_EXPECT(AuthHeaders());
  auto response = CF_EXPECT(HttpGetToFile(http_client_, api_url, dest_path,
                                          headers));

  CF_EXPECT(response.HttpSuccess(),
            "Failed to download from GCS. URL: gs://" << bucket << "/" << object
            << ", HTTP status: " << response.http_code
            << " (" << response.StatusDescription() << ")");

  return {};
}

bool UrlDownloader::IsGoogleHost(const std::string& url) {
  // Extract hostname from URL: skip scheme (https:// or http://)
  std::string_view view = url;
  auto scheme_end = view.find("://");
  if (scheme_end == std::string_view::npos) {
    return false;
  }
  view = view.substr(scheme_end + 3);
  // Extract hostname (up to first '/' or ':' for port)
  auto host_end = view.find_first_of(":/");
  std::string host(view.substr(0, host_end));
  return android::base::EndsWith(host, ".googleapis.com") ||
         android::base::EndsWith(host, ".google.com");
}

Result<void> UrlDownloader::DownloadFromHttp(const std::string& url,
                                             const std::string& dest_path) {
  // Only send auth headers to Google hosts to avoid leaking credentials
  // to third-party servers.
  std::vector<std::string> headers;
  if (IsGoogleHost(url)) {
    headers = CF_EXPECT(AuthHeaders());
  }
  auto response = CF_EXPECT(HttpGetToFile(http_client_, url, dest_path,
                                          headers));

  CF_EXPECT(response.HttpSuccess(),
            "Failed to download from " << url
            << ", HTTP status: " << response.http_code
            << " (" << response.StatusDescription() << ")");

  return {};
}

}  // namespace cuttlefish
