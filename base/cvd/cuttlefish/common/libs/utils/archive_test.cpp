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
// limitations under the License.

#include "common/libs/utils/archive.h"

#include <fstream>
#include <string>
#include <vector>

#include "common/libs/utils/files.h"
#include "common/libs/utils/result.h"
#include "common/libs/utils/result_matchers.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "android-base/file.h"

namespace cuttlefish {

void CreateTempFileWithText(const std::string& filepath,
                            const std::string& text) {
  std::ofstream file(filepath);
  file << text;
  file.close();
}

// Test fixture for common setup and teardown
class ExtractArchiveContentsTests : public ::testing::Test {
 protected:
  void SetUp() override {
    temp_dir_ = TemporaryDir().path;
    mkdir(temp_dir_.c_str(), 0755);
    archive_dir_ = temp_dir_ + "/device-imge-123.zip";
    mkdir(archive_dir_.c_str(), 0755);
    CreateTempFileWithText(archive_dir_ + "/file1.txt", "file1");
    std::string sub_dir = archive_dir_ + "/sub_dir";
    mkdir(sub_dir.c_str(), 0755);
    CreateTempFileWithText(sub_dir + "/file2.txt", "file2");
    extract_dir_ = temp_dir_ + "/target_dir";
    mkdir(extract_dir_.c_str(), 0755);
  }

  void TearDown() override { RecursivelyRemoveDirectory(temp_dir_); }

  std::string temp_dir_;
  std::string archive_dir_;
  std::string extract_dir_;
};

TEST_F(ExtractArchiveContentsTests, ExtractFromNonExistingArchive) {
  std::string archivePath = temp_dir_ + "/nonexistent.zip";
  Result<std::vector<std::string>> result =
      ExtractArchiveContents(archivePath, extract_dir_, false);
  ASSERT_THAT(result, IsError());
}

TEST_F(ExtractArchiveContentsTests, ExtractUncompressedArchive) {
  Result<std::vector<std::string>> result = ExtractArchiveContents(
      archive_dir_, extract_dir_, false);
  ASSERT_THAT(result, IsOk());
  ASSERT_THAT(result.value(),
              testing::UnorderedElementsAre(
                  extract_dir_ + "/file1.txt",
                  extract_dir_ + "/sub_dir/file2.txt"));
  ASSERT_THAT(FileExists(archive_dir_), testing::IsFalse());
}

TEST_F(ExtractArchiveContentsTests, ExtractUncompressedArchiveKeepingArchive) {
  Result<std::vector<std::string>> result = ExtractArchiveContents(
      archive_dir_, extract_dir_, true);
  ASSERT_THAT(result, IsOk());
  ASSERT_THAT(result.value(),
              testing::UnorderedElementsAre(
                  extract_dir_ + "/file1.txt",
                  extract_dir_ + "/sub_dir/file2.txt"));
  ASSERT_THAT(FileExists(archive_dir_), testing::IsTrue());
}

}  // namespace cuttlefish
