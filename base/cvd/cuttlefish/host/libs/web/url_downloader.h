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

#ifndef CUTTLEFISH_HOST_LIBS_WEB_URL_DOWNLOADER_H_
#define CUTTLEFISH_HOST_LIBS_WEB_URL_DOWNLOADER_H_

#include <string>
#include <vector>

#include "cuttlefish/host/libs/web/credential_source.h"
#include "cuttlefish/host/libs/web/http_client/http_client.h"
#include "cuttlefish/result/result.h"

namespace cuttlefish {

// Supported URL schemes for artifact downloads.
enum class UrlScheme {
  kGcs,    // gs://bucket/object
  kHttps,  // https://host/path
  kHttp,   // http://host/path
};

// Parses a URL and returns its scheme.
Result<UrlScheme> ParseUrlScheme(const std::string& url);

// Extracts the filename from a URL path.
// For "gs://bucket/path/file.zip" returns "file.zip".
// For "https://example.com/file.zip?token=abc" returns "file.zip".
std::string FilenameFromUrl(const std::string& url);

// Downloads artifacts from URLs (gs://, https://, http://).
// Reuses existing HttpClient and CredentialSource infrastructure.
//
// GCS URLs (gs://bucket/object) are translated to the Google Cloud Storage
// JSON API. HTTPS/HTTP URLs are downloaded directly.
//
// Example usage:
//   UrlDownloader downloader(http_client, credential_source);
//   auto path = downloader.Download("gs://bucket/image.zip", "/tmp/images");
//   // path = "/tmp/images/image.zip"
class UrlDownloader {
 public:
  // Creates a UrlDownloader.
  // credential_source may be nullptr for public URLs that don't require auth.
  UrlDownloader(HttpClient& http_client, CredentialSource* credential_source);

  // Downloads a URL to the specified directory.
  // Returns the path to the downloaded file.
  Result<std::string> Download(const std::string& url,
                               const std::string& target_directory);

  // Downloads multiple URLs to the specified directory.
  // Returns paths to all downloaded files in the same order as input URLs.
  Result<std::vector<std::string>> DownloadAll(
      const std::vector<std::string>& urls,
      const std::string& target_directory);

  // Returns true if the URL hostname is a Google domain
  // (*.googleapis.com or *.google.com).
  static bool IsGoogleHost(const std::string& url);

 private:
  Result<void> DownloadToFile(const std::string& url,
                              const std::string& dest_path);

  // GCS: gs://bucket/object → Storage JSON API
  Result<void> DownloadFromGcs(const std::string& bucket,
                               const std::string& object,
                               const std::string& dest_path);

  // HTTPS/HTTP: Direct download
  Result<void> DownloadFromHttp(const std::string& url,
                                const std::string& dest_path);

  Result<std::vector<std::string>> AuthHeaders();

  HttpClient& http_client_;
  CredentialSource* credential_source_;  // nullable for public URLs
};

}  // namespace cuttlefish

#endif  // CUTTLEFISH_HOST_LIBS_WEB_URL_DOWNLOADER_H_
