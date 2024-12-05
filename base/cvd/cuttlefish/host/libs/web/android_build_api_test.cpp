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
// limitations under the License

#include "host/libs/web/android_build_api.h"

#include <chrono>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "android-base/file.h"
#include "common/libs/utils/files.h"
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

class AndroidBuildApiTests : public ::testing::Test {
 protected:
  void SetUp() override {
    target_dir_ = std::string(temp_dir_.path) + "/target_dir";
    Result<void> result = EnsureDirectoryExists(target_dir_, 0755);
    if (!result.ok()) {
      FAIL() << result.error().FormatForEnv();
    }
    mock_cas_downloader_ =
        std::make_unique<MockCasDownloader>("", std::vector<std::string>{});
    mock_http_client_ = std::make_unique<MockHttpClient>();
  }

  TemporaryDir temp_dir_;
  std::string target_dir_;
  std::unique_ptr<MockCasDownloader> mock_cas_downloader_;
  std::unique_ptr<MockHttpClient> mock_http_client_;
};

using testing::_;
using testing::Return;
using testing::StrEq;
using testing::WithArg;

TEST_F(AndroidBuildApiTests, DownloadFileWithCasDownloaderIfSet) {
  // HttpClient::DownloadToJson is called three times:
  // 1. to get the list of matching artifacts.
  // 2. to get the list of artifacts matching "cas_digests.json".
  // 3. to get the url for "cas_digests.json".
  Json::Value artifact_list_data = Json::objectValue;
  artifact_list_data["artifacts"] = Json::arrayValue;
  artifact_list_data["artifacts"][0]["name"] = "device-img-123.zip";
  Json::Value cas_digests_data = Json::objectValue;
  cas_digests_data["artifacts"] = Json::arrayValue;
  cas_digests_data["artifacts"][0]["name"] = "cas_digests.json";
  Json::Value cas_digests_url_data = Json::objectValue;
  cas_digests_url_data["signedUrl"] = "urlForCasDigestsJson";
  EXPECT_CALL(*mock_http_client_, UrlEscape(_))
      .WillRepeatedly(WithArg<0>([](const std::string& s) { return s; }));
  EXPECT_CALL(*mock_http_client_, DownloadToJson)
      .Times(3)
      .WillOnce(Return(HttpResponse<Json::Value>{artifact_list_data, 200}))
      .WillOnce(Return(HttpResponse<Json::Value>{cas_digests_data, 200}))
      .WillOnce(Return(HttpResponse<Json::Value>{cas_digests_url_data, 200}));

  // HttpClient::DownloadToFile is called once to download "cas_digests.json".
  EXPECT_CALL(*mock_http_client_, DownloadToFile)
      .Times(1)
      .WillOnce(Return(HttpResponse<std::string>{"cas_digest.json", 200}));

  // CasDownloader::DownloadFile is called once to return successfully after
  // downloading "cas_digests.json".
  EXPECT_CALL(*mock_cas_downloader_, DownloadFile)
      .Times(1)
      .WillOnce(WithArg<4>([](const DigestsFetcher& digests_fetcher) {
        Result<std::string> result = digests_fetcher("cas_digests.json");
        EXPECT_THAT(result, IsOk());
        return Result<void>{};
      }));

  AndroidBuildApi api(std::move(mock_http_client_), nullptr, nullptr, "api_key",
                      std::chrono::seconds(1), "api_base_url", "project_id",
                      std::move(mock_cas_downloader_));

  Result<std::string> result =
      api.DownloadFile(DeviceBuild("branch", "target", "filepath"), target_dir_,
                       "device-img-123.zip");
  EXPECT_THAT(result, IsOk());
}

TEST_F(AndroidBuildApiTests, DownloadFileWithHttpClientIfArtifactNotOnCas) {
  // HttpClient::DownloadToJson is called first to get the list of matching
  // artifacts, then the url for the artifact.
  Json::Value artifact_list_data = Json::objectValue;
  artifact_list_data["artifacts"] = Json::arrayValue;
  artifact_list_data["artifacts"][0]["name"] = "device-img-123.zip";
  Json::Value artifact_data = Json::objectValue;
  artifact_data["signedUrl"] = "signedUrl";
  EXPECT_CALL(*mock_http_client_, UrlEscape(_))
      .WillRepeatedly(WithArg<0>([](const std::string& s) { return s; }));
  EXPECT_CALL(*mock_http_client_, DownloadToJson)
      .Times(2)
      .WillOnce(Return(HttpResponse<Json::Value>{artifact_list_data, 200}))
      .WillOnce(Return(HttpResponse<Json::Value>{artifact_data, 200}));

  // HttpClient::DownloadToFile is called once to download the artifact.
  EXPECT_CALL(*mock_http_client_, DownloadToFile)
      .Times(1)
      .WillOnce(Return(HttpResponse<std::string>{"device-img-123.zip", 200}));

  // CasDownloader::DownloadFile returns an error as the artifact is not on CAS.
  EXPECT_CALL(*mock_cas_downloader_, DownloadFile)
      .Times(1)
      .WillOnce(
          Return(CF_ERR("CAS digest for 'device-img-123.zip' not found.")));

  AndroidBuildApi api(std::move(mock_http_client_), nullptr, nullptr, "api_key",
                      std::chrono::seconds(1), "api_base_url", "project_id",
                      std::move(mock_cas_downloader_));

  Result<std::string> result =
      api.DownloadFile(DeviceBuild("branch", "target", "filepath"), target_dir_,
                       "device-img-123.zip");
  EXPECT_THAT(result, IsOk());
}

TEST_F(AndroidBuildApiTests, DownloadFileWithHttpClientIfNotDeviceImage) {
  // HttpClient::DownloadToJson is called first to get the list of matching
  // artifacts, then the url for the artifact.
  Json::Value artifact_list_data = Json::objectValue;
  artifact_list_data["artifacts"] = Json::arrayValue;
  artifact_list_data["artifacts"][0]["name"] = "misc_info.txt";
  Json::Value artifact_data = Json::objectValue;
  artifact_data["signedUrl"] = "signedUrl";
  EXPECT_CALL(*mock_http_client_, UrlEscape(_))
      .WillRepeatedly(WithArg<0>([](const std::string& s) { return s; }));
  EXPECT_CALL(*mock_http_client_, DownloadToJson)
      .Times(2)
      .WillOnce(Return(HttpResponse<Json::Value>{artifact_list_data, 200}))
      .WillOnce(Return(HttpResponse<Json::Value>{artifact_data, 200}));

  // HttpClient::DownloadToFile is called once to download the artifact.
  EXPECT_CALL(*mock_http_client_, DownloadToFile)
      .Times(1)
      .WillOnce(Return(HttpResponse<std::string>{"misc_info.txt", 200}));

  // CasDownloader::DownloadFile is not called as it is not a device image.
  EXPECT_CALL(*mock_cas_downloader_, DownloadFile).Times(0);

  AndroidBuildApi api(std::move(mock_http_client_), nullptr, nullptr, "api_key",
                      std::chrono::seconds(1), "api_base_url", "project_id",
                      std::move(mock_cas_downloader_));

  Result<std::string> result =
      api.DownloadFile(DeviceBuild("branch", "target", "filepath"), target_dir_,
                       "misc_info.txt");
  EXPECT_THAT(result, IsOk());
}

TEST_F(AndroidBuildApiTests, DownloadWithoutCasDownloaderIfDirectoryBuild) {
  // The mocks are not called for directory builds.
  EXPECT_CALL(*mock_http_client_, DownloadToJson).Times(0);
  EXPECT_CALL(*mock_http_client_, DownloadToFile).Times(0);
  EXPECT_CALL(*mock_cas_downloader_, DownloadFile).Times(0);

  AndroidBuildApi api(std::move(mock_http_client_), nullptr, nullptr, "api_key",
                      std::chrono::seconds(1), "api_base_url", "project_id",
                      std::move(mock_cas_downloader_));
  std::string path = std::string(temp_dir_.path) + "/any_dir";
  mkdir(path.c_str(), 0755);
  Result<std::string> result = api.DownloadFile(
      DirectoryBuild(std::vector<std::string>{path}, "target", "filepath"),
      "target_dir", "any.file");
  EXPECT_THAT(result, IsError());
  EXPECT_THAT(result.error().Message(),
              StrEq("Target (paths=\"" + path +
                    "\", target=\"target\", filepath=\"filepath\") "
                    "did not contain any.file"));
}

}  // namespace cuttlefish
