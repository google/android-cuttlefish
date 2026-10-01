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

#include "cuttlefish/host/libs/web/caching_build_api.h"

#include <sys/stat.h>
#include <sys/types.h>

#include <string>

#include "android-base/file.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

#include "cuttlefish/common/libs/utils/files.h"
#include "cuttlefish/files/directory_exists.h"
#include "cuttlefish/files/file_exists.h"
#include "cuttlefish/files/recursively_remove_directory.h"
#include "cuttlefish/host/libs/web/android_build.h"
#include "cuttlefish/host/libs/web/android_build_string.h"
#include "cuttlefish/host/libs/web/build_api.h"
#include "cuttlefish/host/libs/zip/libzip_cc/seekable_source.h"
#include "cuttlefish/result/result.h"
#include "cuttlefish/result/result_matchers.h"

namespace cuttlefish {
namespace {

// Writes artifacts ending in "_dir" as directories, as the CAS downloader does
// with `prefer-uncompressed`, and everything else as a regular file.
class FakeBuildApi : public BuildApi {
 public:
  Result<Build> GetBuild(const BuildString&) override {
    return CF_ERR("Not implemented");
  }

  Result<std::string> DownloadFile(const Build&,
                                   const std::string& target_directory,
                                   const std::string& artifact_name) override {
    download_count++;
    const std::string path = target_directory + "/" + artifact_name;
    if (artifact_name.ends_with("_dir")) {
      CF_EXPECT(EnsureDirectoryExists(path + "/nested"));
      CF_EXPECT(WriteNewFile(path + "/a.img", "a"));
      CF_EXPECT(WriteNewFile(path + "/nested/b.img", "b"));
    } else {
      CF_EXPECT(WriteNewFile(path, "file"));
    }
    return path;
  }

  Result<SeekableZipSource> FileReader(const Build&,
                                       const std::string&) override {
    return CF_ERR("Not implemented");
  }

  int download_count = 0;
};

ino_t Inode(const std::string& path) {
  struct stat st{};
  EXPECT_EQ(stat(path.c_str(), &st), 0) << path;
  return st.st_ino;
}

class CachingBuildApiTest : public ::testing::Test {
 protected:
  CachingBuildApiTest()
      : cache_dir_(std::string(temp_dir_.path) + "/cache"),
        target_dir_(std::string(temp_dir_.path) + "/target"),
        caching_api_(fake_api_, cache_dir_) {}

  TemporaryDir temp_dir_;
  std::string cache_dir_;
  std::string target_dir_;
  FakeBuildApi fake_api_;
  CachingBuildApi caching_api_;
  Build build_ = DeviceBuild{.id = "123", .target = "target"};
};

TEST_F(CachingBuildApiTest, LinksFileFromCache) {
  const Result<std::string> path =
      caching_api_.DownloadFile(build_, target_dir_, "artifact.zip");
  ASSERT_THAT(path, IsOkAndValue(target_dir_ + "/artifact.zip"));
  ASSERT_THAT(caching_api_.DownloadFile(build_, target_dir_, "artifact.zip"),
              IsOk());

  EXPECT_EQ(fake_api_.download_count, 1);
  EXPECT_EQ(Inode(*path), Inode(cache_dir_ + "/123/target/artifact.zip"));
}

TEST_F(CachingBuildApiTest, LinksDirectoryContentsFromCache) {
  const std::string target = target_dir_ + "/artifact_dir";
  const std::string cached = cache_dir_ + "/123/target/artifact_dir";

  ASSERT_THAT(caching_api_.DownloadFile(build_, target_dir_, "artifact_dir"),
              IsOkAndValue(target));
  // A second fetch into a fresh target directory is served from the cache.
  ASSERT_THAT(RecursivelyRemoveDirectory(target_dir_), IsOk());
  ASSERT_THAT(caching_api_.DownloadFile(build_, target_dir_, "artifact_dir"),
              IsOkAndValue(target));

  EXPECT_EQ(fake_api_.download_count, 1);
  EXPECT_TRUE(DirectoryExists(target));
  EXPECT_TRUE(FileExists(target + "/a.img"));
  EXPECT_TRUE(FileExists(target + "/nested/b.img"));
  EXPECT_EQ(Inode(target + "/nested/b.img"), Inode(cached + "/nested/b.img"));
}

}  // namespace
}  // namespace cuttlefish
