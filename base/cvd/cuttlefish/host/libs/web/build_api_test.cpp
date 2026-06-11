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

#include "cuttlefish/host/libs/web/build_api.h"

#include <string>

#include "gmock/gmock.h"
#include "gtest/gtest.h"

#include "cuttlefish/host/libs/web/android_build.h"
#include "cuttlefish/host/libs/web/android_build_string.h"
#include "cuttlefish/host/libs/zip/libzip_cc/seekable_source.h"
#include "cuttlefish/result/result.h"
#include "cuttlefish/result/result_matchers.h"

namespace cuttlefish {
namespace {

using ::testing::_;
using ::testing::Return;

class MockBuildApi : public BuildApi {
 public:
  MOCK_METHOD(Result<Build>, GetBuild, (const BuildString&), (override));
  MOCK_METHOD(Result<std::string>, DownloadFile,
              (const Build&, const std::string&, const std::string&),
              (override));
  MOCK_METHOD(Result<SeekableZipSource>, FileReader,
              (const Build&, const std::string&), (override));
};

TEST(BuildApiTest, DownloadFileWithBackupSuccessFirst) {
  MockBuildApi mock_api;
  Build build = DeviceBuild{.id = "123", .target = "test"};
  std::string target_dir = "/tmp";
  std::string artifact_name = "primary.zip";
  std::string backup_artifact_name = "backup.zip";
  std::string expected_path = "/tmp/primary.zip";

  EXPECT_CALL(mock_api, DownloadFile(_, target_dir, artifact_name))
      .WillOnce(Return(expected_path));
  EXPECT_CALL(mock_api, DownloadFile(_, target_dir, backup_artifact_name))
      .Times(0);

  Result<std::string> result = DownloadFileWithBackup(
      mock_api, build, target_dir, artifact_name, backup_artifact_name);

  ASSERT_THAT(result, IsOkAndValue(expected_path));
}

TEST(BuildApiTest, DownloadFileWithBackupFallback) {
  MockBuildApi mock_api;
  Build build = DeviceBuild{.id = "123", .target = "test"};
  std::string target_dir = "/tmp";
  std::string artifact_name = "primary.zip";
  std::string backup_artifact_name = "backup.zip";
  std::string expected_path = "/tmp/backup.zip";

  EXPECT_CALL(mock_api, DownloadFile(_, target_dir, artifact_name))
      .WillOnce(Return(CF_ERR("File not found")));
  EXPECT_CALL(mock_api, DownloadFile(_, target_dir, backup_artifact_name))
      .WillOnce(Return(expected_path));

  Result<std::string> result = DownloadFileWithBackup(
      mock_api, build, target_dir, artifact_name, backup_artifact_name);

  ASSERT_THAT(result, IsOkAndValue(expected_path));
}

TEST(BuildApiTest, DownloadFileWithBackupBothFail) {
  MockBuildApi mock_api;
  Build build = DeviceBuild{.id = "123", .target = "test"};
  std::string target_dir = "/tmp";
  std::string artifact_name = "primary.zip";
  std::string backup_artifact_name = "backup.zip";

  EXPECT_CALL(mock_api, DownloadFile(_, target_dir, artifact_name))
      .WillOnce(Return(CF_ERR("Primary file not found")));
  EXPECT_CALL(mock_api, DownloadFile(_, target_dir, backup_artifact_name))
      .WillOnce(Return(CF_ERR("Backup file not found")));

  Result<std::string> result = DownloadFileWithBackup(
      mock_api, build, target_dir, artifact_name, backup_artifact_name);

  ASSERT_THAT(result, IsError());
}

}  // namespace
}  // namespace cuttlefish
