//
// Copyright (C) 2024 The Android Open Source Project
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
// limitations under the License#include "host/libs/web/cas_downloader.h"

#include "host/libs/web/caching_build_api.h"

#include <chrono>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "common/libs/utils/result.h"
#include "common/libs/utils/result_matchers.h"
#include "host/libs/web/build_api.h"
#include "host/libs/web/cas/cas_downloader.h"
#include "host/libs/web/http_client/http_client.h"
#include "json/value.h"
#include <gmock/gmock.h>
#include <gtest/gtest.h>

namespace cuttlefish {

class MockHttpClient : public HttpClient {
 public:
  virtual ~MockHttpClient() = default;
  MOCK_METHOD(Result<HttpResponse<std::string>>, GetToString,
              (const std::string& url, const std::vector<std::string>& headers),
              (override));
  MOCK_METHOD(Result<HttpResponse<std::string>>, PostToString,
              (const std::string& url, const std::string& data,
               const std::vector<std::string>& headers),
              (override));
  MOCK_METHOD(Result<HttpResponse<std::string>>, DeleteToString,
              (const std::string& url, const std::vector<std::string>& headers),
              (override));
  MOCK_METHOD(Result<HttpResponse<Json::Value>>, PostToJson,
              (const std::string& url, const std::string& data,
               const std::vector<std::string>& headers),
              (override));
  MOCK_METHOD(Result<HttpResponse<Json::Value>>, PostToJson,
              (const std::string& url, const Json::Value& data,
               const std::vector<std::string>& headers),
              (override));
  MOCK_METHOD(Result<HttpResponse<Json::Value>>, DownloadToJson,
              (const std::string& url, const std::vector<std::string>& headers),
              (override));
  MOCK_METHOD(Result<HttpResponse<Json::Value>>, DeleteToJson,
              (const std::string& url, const std::vector<std::string>& headers),
              (override));
  MOCK_METHOD(Result<HttpResponse<std::string>>, DownloadToFile,
              (const std::string& url, const std::string& path,
               const std::vector<std::string>& headers),
              (override));
  MOCK_METHOD(Result<HttpResponse<void>>, DownloadToCallback,
              (DataCallback callback, const std::string& url,
               const std::vector<std::string>& headers),
              (override));
  MOCK_METHOD(std::string, UrlEscape, (const std::string&), (override));
};

using testing::_;
using testing::Return;
using testing::WithArg;

class MockCasDownloader : public CasDownloader {
 public:
  MockCasDownloader(std::string downloader_path, std::vector<std::string> flags,
                    bool prefer_uncompressed = false)
      : CasDownloader(std::move(downloader_path), std::move(flags),
                      prefer_uncompressed) {}

  MOCK_METHOD(Result<void>, DownloadFile,
              (const std::string& build_id, const std::string& build_target,
               const std::string& artifact_name,
               const std::string& target_directory,
               const DigestsFetcher& digests_fetcher,
               const std::optional<std::string>& stats_filepath),
              (override));
};

class CachingBuildApiTests : public ::testing::Test {
 protected:
  void SetUp() override {
    mock_cas_downloader_ =
        std::make_unique<MockCasDownloader>("", std::vector<std::string>{});
    mock_http_client_ = std::make_unique<MockHttpClient>();
  }

  std::unique_ptr<MockCasDownloader> mock_cas_downloader_;
  std::unique_ptr<MockHttpClient> mock_http_client_;
};

TEST_F(CachingBuildApiTests, DownloadFileWithCasDownloaderIfSet) {
  // HttpClient::DownloadToJson is called once to get the list of matching
  // artifacts. The artifact itself is downloaded from CAS.
  Json::Value artifact_list_data = Json::objectValue;
  artifact_list_data["artifacts"] = Json::arrayValue;
  artifact_list_data["artifacts"][0]["name"] = "device-img-123.zip";
  Json::Value artifact_data = Json::objectValue;
  artifact_data["signedUrl"] = "signedUrl";
  EXPECT_CALL(*mock_http_client_, UrlEscape(_))
      .WillRepeatedly(
          WithArg<0>([](const std::string& s) { return s; }));
  EXPECT_CALL(*mock_http_client_, DownloadToJson)
      .Times(1)
      .WillOnce(Return(HttpResponse<Json::Value>{artifact_list_data, 200}));

  // HttpClient::DownloadToFile is not called as the artifact is on CAS.
  EXPECT_CALL(*mock_http_client_, DownloadToFile).Times(0);

  // CasDownloader::DownloadFile is called once to return successfully after
  // downloading the artifact.
  EXPECT_CALL(*mock_cas_downloader_, DownloadFile)
      .Times(1)
      .WillOnce(Return(Result<void>{}));

  std::unique_ptr<BuildApi> api =
      CreateBuildApi(std::move(mock_http_client_), nullptr, nullptr, "api_key",
                     std::chrono::seconds(1), "api_base_url", "project_id",
                     false, "cache_base_path", std::move(mock_cas_downloader_));

  Result<std::string> result =
      api->DownloadFile(DeviceBuild("branch", "target", "filepath"),
                        "target_dir", "device-img-123.zip");
  EXPECT_THAT(result, IsOk());
}

}  // namespace cuttlefish
