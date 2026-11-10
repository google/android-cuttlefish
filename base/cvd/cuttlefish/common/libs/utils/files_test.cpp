//
// Copyright (C) 2023 The Android Open Source Project
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

#include "cuttlefish/common/libs/utils/files.h"

#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cstdint>
#include <fstream>
#include <ios>
#include <string>
#include <thread>
#include <vector>

#include "absl/cleanup/cleanup.h"
#include "absl/strings/str_cat.h"
#include "android-base/file.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

#include "cuttlefish/files/are_hard_linked.h"
#include "cuttlefish/files/file_exists.h"
#include "cuttlefish/files/is_directory_empty.h"
#include "cuttlefish/result/result.h"
#include "cuttlefish/result/result_matchers.h"

namespace cuttlefish {

using testing::IsTrue;

void CreateTempFileWithText(const std::string& filepath,
                            const std::string& text) {
  std::ofstream file(filepath);
  file << text;
  file.close();
}

class FilesTests : public ::testing::Test {
 protected:
  Result<void> CreateTestDirs() {
    src_dir_ = std::string(temp_dir_.path) + "/device-imge-123.zip";
    CF_EXPECT(EnsureDirectoryExists(src_dir_, 0755));
    CreateTempFileWithText(src_dir_ + "/file1.txt", "file1");
    std::string sub_dir = src_dir_ + "/sub_dir";
    CF_EXPECT(EnsureDirectoryExists(sub_dir.c_str(), 0755));
    CreateTempFileWithText(sub_dir + "/file2.txt", "file2");
    dst_dir_ = std::string(temp_dir_.path) + "/target_dir";
    return {};
  }

  void SetUp() override {
    Result<void> result = CreateTestDirs();
    if (!result.has_value()) {
      FAIL() << result.error();
    }
  }

  TemporaryDir temp_dir_;
  std::string src_dir_;
  std::string dst_dir_;
};

TEST_F(FilesTests, LinkOrCopyRecursivelyFailsIfSourceIsNotADirectory) {
  Result<void> result = LinkOrCopyDirectoryContentsRecursively(
      src_dir_ + "/file1.txt", dst_dir_ + "/file1.txt");

  EXPECT_THAT(result, IsError());
}

TEST_F(FilesTests, LinkOrCopyRecursively) {
  Result<void> result =
      LinkOrCopyDirectoryContentsRecursively(src_dir_, dst_dir_);

  EXPECT_THAT(result, IsOk());
  Result<bool> resultHardLinked =
      AreHardLinked(src_dir_ + "/file1.txt", dst_dir_ + "/file1.txt");
  EXPECT_THAT(resultHardLinked, IsOk());
  EXPECT_THAT(resultHardLinked.value(), IsTrue());
  resultHardLinked = AreHardLinked(src_dir_ + "/sub_dir/file2.txt",
                                   dst_dir_ + "/sub_dir/file2.txt");
  EXPECT_THAT(resultHardLinked, IsOk());
  EXPECT_THAT(resultHardLinked.value(), IsTrue());
}

TEST_F(FilesTests, WalkDirectoryFilesRecursesAndSkipsDirectories) {
  std::vector<std::string> visited;
  Result<void> result = WalkDirectoryFiles(
      src_dir_, [&visited](const std::string& path) -> Result<void> {
        visited.push_back(path);
        return {};
      });

  EXPECT_THAT(result, IsOk());
  // `sub_dir` itself must not be reported, but the file inside it must be.
  EXPECT_THAT(visited,
              testing::UnorderedElementsAre(src_dir_ + "/file1.txt",
                                            src_dir_ + "/sub_dir/file2.txt"));
}

TEST_F(FilesTests, MoveDirectoryContentsFailsIfSourceIsNotADirectory) {
  Result<void> result =
      MoveDirectoryContents(src_dir_ + "/file1.txt", dst_dir_ + "/file1.txt");
  EXPECT_THAT(result, IsError());
}

TEST_F(FilesTests, MoveDirectoryContents) {
  Result<void> result = MoveDirectoryContents(src_dir_, dst_dir_);
  EXPECT_THAT(result, IsOk());
  EXPECT_THAT(IsDirectoryEmpty(src_dir_), IsOkAndValue(true));
  EXPECT_THAT(FileExists(dst_dir_ + "/file1.txt"), IsTrue());
  EXPECT_THAT(FileExists(dst_dir_ + "/sub_dir/file2.txt"), IsTrue());
}

TEST(FilesTest, PathWithCustomEnv) {
  const std::string env_name = "TEST_PATH";
  const std::string dir1 = "/foo/bar";
  const std::string dir2 = "/baz";
  const std::string env_value = absl::StrCat(dir1, ":", dir2);
  setenv(env_name.c_str(), env_value.c_str(), 1);
  absl::Cleanup cleanup = [&env_name]() { unsetenv(env_name.c_str()); };

  std::vector<std::string> result = Path(env_name);
  ASSERT_EQ(result.size(), 2);
  EXPECT_EQ(result[0], dir1);
  EXPECT_EQ(result[1], dir2);
}

TEST(FilesTest, PathWithSecondCustomEnv) {
  const std::string env_name = "TEST_PATH";
  const std::string dir = "/foo";
  setenv(env_name.c_str(), dir.c_str(), 1);
  absl::Cleanup cleanup = [&env_name]() { unsetenv(env_name.c_str()); };

  auto result = Path(env_name);
  ASSERT_EQ(result.size(), 1);
  EXPECT_EQ(result[0], dir);
}

TEST(FilesTest, SearchFindsFile) {
  const std::string temp_dir = testing::TempDir();
  const std::string file_name = "search_test_file";
  const std::string full_path = absl::StrCat(temp_dir, "/", file_name);

  EXPECT_TRUE(android::base::WriteStringToFile("", full_path));
  absl::Cleanup cleanup = [full_path]() { unlink(full_path.c_str()); };

  auto result = Search({temp_dir}, file_name);
  EXPECT_THAT(result, IsOkAndValue(full_path));
}

TEST(FilesTest, SearchFileNotFoundReturnsError) {
  const std::string temp_dir = testing::TempDir();
  EXPECT_FALSE(FileExists(absl::StrCat(temp_dir, "/search_non_existent_file")));

  auto result = Search({temp_dir}, "search_non_existent_file");
  EXPECT_FALSE(result.has_value());
}

TEST_F(FilesTests, EnsureDirectoryExistsSetsRequestedModeDespiteUmask) {
  const mode_t old_umask = umask(0022);
  absl::Cleanup restore_umask = [old_umask]() { umask(old_umask); };

  const std::string shared_dir = std::string(temp_dir_.path) + "/shared/lock";
  EXPECT_THAT(EnsureDirectoryExists(shared_dir, 0777), IsOk());

  struct stat st = {};
  ASSERT_EQ(stat(shared_dir.c_str(), &st), 0);
  EXPECT_EQ(st.st_mode & 0777, 0777u);
}

TEST_F(FilesTests, EnsureDirectoryExistsFailsWhenRegularFileExists) {
  const std::string file_path = src_dir_ + "/file1.txt";
  EXPECT_THAT(EnsureDirectoryExists(file_path, 0777), IsError());
}

TEST_F(FilesTests, EnsureDirectoryExistsConcurrentCallsSucceed) {
  const std::string base = std::string(temp_dir_.path) + "/concurrent/a/b/c/d";
  constexpr size_t kNumThreads = 32;
  std::vector<std::thread> threads;
  std::vector<Result<void>> results(kNumThreads);
  threads.reserve(kNumThreads);
  for (size_t i = 0; i < kNumThreads; ++i) {
    threads.emplace_back([&base, &results, i]() {
      results[i] = EnsureDirectoryExists(base, 0777);
    });
  }
  for (std::thread& t : threads) {
    t.join();
  }
  for (size_t i = 0; i < kNumThreads; ++i) {
    EXPECT_THAT(results[i], IsOk());
  }
  struct stat st = {};
  ASSERT_EQ(stat(base.c_str(), &st), 0);
  EXPECT_EQ(st.st_mode & 0777, 0777u);
}

TEST(FilesTest, ReadBinaryFileContentsReadsArbitraryBytes) {
  TemporaryDir temp_dir;
  const std::string file_path = absl::StrCat(temp_dir.path, "/binary.bin");
  const std::vector<uint8_t> expected = {0x52, 0x49, 0x46, 0x46, 0x00,
                                         0xFF, 0x80, 0x7F, 0x01};
  {
    std::ofstream out(file_path, std::ios::out | std::ios::binary);
    out.write(reinterpret_cast<const char*>(expected.data()), expected.size());
  }

  Result<std::vector<uint8_t>> actual = ReadBinaryFileContents(file_path);
  EXPECT_THAT(actual, IsOkAndValue(expected));
}

TEST(FilesTest, ReadBinaryFileContentsEmptyFileReturnsEmptyVector) {
  TemporaryDir temp_dir;
  const std::string file_path = absl::StrCat(temp_dir.path, "/empty.bin");
  {
    std::ofstream out(file_path, std::ios::out | std::ios::binary);
  }

  Result<std::vector<uint8_t>> actual = ReadBinaryFileContents(file_path);
  EXPECT_THAT(actual, IsOkAndValue(std::vector<uint8_t>{}));
}

TEST(FilesTest, ReadBinaryFileContentsSpansMultipleChunks) {
  TemporaryDir temp_dir;
  const std::string file_path = absl::StrCat(temp_dir.path, "/large.bin");
  std::vector<uint8_t> expected((2 << 16) + 1234);
  for (size_t i = 0; i < expected.size(); ++i) {
    expected[i] = static_cast<uint8_t>((i * 31 + 7) & 0xFF);
  }
  {
    std::ofstream out(file_path, std::ios::out | std::ios::binary);
    out.write(reinterpret_cast<const char*>(expected.data()), expected.size());
  }

  Result<std::vector<uint8_t>> actual = ReadBinaryFileContents(file_path);
  EXPECT_THAT(actual, IsOkAndValue(expected));
}

TEST(FilesTest, ReadBinaryFileContentsMissingFileReturnsError) {
  TemporaryDir temp_dir;
  Result<std::vector<uint8_t>> actual =
      ReadBinaryFileContents(absl::StrCat(temp_dir.path, "/missing.bin"));
  EXPECT_THAT(actual, IsError());
}

}  // namespace cuttlefish
