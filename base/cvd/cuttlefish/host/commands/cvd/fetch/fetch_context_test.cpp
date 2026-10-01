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

#include "cuttlefish/host/commands/cvd/fetch/fetch_context.h"

#include <sys/stat.h>
#include <sys/types.h>

#include <map>
#include <optional>
#include <string>

#include "android-base/file.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

#include "cuttlefish/common/libs/utils/files.h"
#include "cuttlefish/files/directory_exists.h"
#include "cuttlefish/files/file_exists.h"
#include "cuttlefish/host/commands/cvd/fetch/builds.h"
#include "cuttlefish/host/commands/cvd/fetch/fetch_tracer.h"
#include "cuttlefish/host/commands/cvd/fetch/target_directories.h"
#include "cuttlefish/host/libs/config/fetcher_config.h"
#include "cuttlefish/host/libs/web/android_build.h"
#include "cuttlefish/host/libs/web/android_build_string.h"
#include "cuttlefish/host/libs/web/build_api.h"
#include "cuttlefish/host/libs/zip/libzip_cc/seekable_source.h"
#include "cuttlefish/result/result.h"
#include "cuttlefish/result/result_matchers.h"

namespace cuttlefish {
namespace {

using ::testing::Contains;
using ::testing::Key;
using ::testing::Not;

constexpr char kImgZip[] = "product-img-123.zip";

// Delivers every artifact as an already-extracted directory, the way the CAS
// downloader does with `prefer-uncompressed`.
class UncompressedBuildApi : public BuildApi {
 public:
  Result<Build> GetBuild(const BuildString&) override {
    return CF_ERR("Not implemented");
  }

  Result<std::string> DownloadFile(const Build&,
                                   const std::string& target_directory,
                                   const std::string& artifact_name) override {
    const std::string dir = target_directory + "/" + artifact_name;
    CF_EXPECT(EnsureDirectoryExists(dir + "/nested"));
    CF_EXPECT(WriteNewFile(dir + "/android-info.txt", "info"));
    CF_EXPECT(WriteNewFile(dir + "/super.img", "super"));
    CF_EXPECT(WriteNewFile(dir + "/vbmeta.img", "vbmeta"));
    CF_EXPECT(WriteNewFile(dir + "/nested/file.txt", "x"));
    return dir;
  }

  Result<SeekableZipSource> FileReader(const Build&,
                                       const std::string&) override {
    return CF_ERR("Not implemented");
  }
};

ino_t Inode(const std::string& path) {
  struct stat st{};
  EXPECT_EQ(stat(path.c_str(), &st), 0) << path;
  return st.st_ino;
}

class FetchArtifactDirectoryTest : public ::testing::Test {
 protected:
  FetchArtifactDirectoryTest()
      : target_directories_{.root = temp_dir_.path},
        builds_{.default_build = DeviceBuild{.id = "123",
                                             .target = "product-userdebug",
                                             .product = "product"}},
        context_(build_api_, target_directories_, builds_, fetcher_config_,
                 tracer_) {}

  std::string Root() const { return temp_dir_.path; }

  TemporaryDir temp_dir_;
  UncompressedBuildApi build_api_;
  TargetDirectories target_directories_;
  Builds builds_;
  FetcherConfig fetcher_config_;
  FetchTracer tracer_;
  FetchContext context_;
};

TEST_F(FetchArtifactDirectoryTest, ExtractAllLinksDirectoryContents) {
  std::optional<FetchBuildContext> build = context_.DefaultBuild();
  ASSERT_TRUE(build.has_value());
  FetchArtifact img_zip = build->Artifact(kImgZip);

  ASSERT_THAT(img_zip.Download(), IsOk());
  ASSERT_THAT(img_zip.ExtractAll(), IsOk());

  const std::string artifact_dir = Root() + "/" + kImgZip;
  EXPECT_TRUE(DirectoryExists(artifact_dir));
  for (const char* file :
       {"android-info.txt", "super.img", "vbmeta.img", "nested/file.txt"}) {
    EXPECT_TRUE(FileExists(Root() + "/" + file)) << file;
  }
  EXPECT_EQ(Inode(Root() + "/super.img"), Inode(artifact_dir + "/super.img"));
  EXPECT_NE(Inode(Root() + "/vbmeta.img"), Inode(artifact_dir + "/vbmeta.img"));
  EXPECT_THAT(ReadFileContents(artifact_dir + "/vbmeta.img"),
              IsOkAndValue("vbmeta"));

  const std::map<std::string, CvdFile> files = fetcher_config_.get_cvd_files();
  EXPECT_THAT(files, Contains(Key("android-info.txt")));
  EXPECT_THAT(files, Contains(Key("super.img")));
  EXPECT_THAT(files, Contains(Key("nested/file.txt")));
  EXPECT_THAT(files, Not(Contains(Key(kImgZip))));
  EXPECT_EQ(files.at("super.img").archive_source, kImgZip);
}

TEST_F(FetchArtifactDirectoryTest, DeleteLocalFileRemovesDirectory) {
  std::optional<FetchBuildContext> build = context_.DefaultBuild();
  ASSERT_TRUE(build.has_value());
  FetchArtifact img_zip = build->Artifact(kImgZip);

  ASSERT_THAT(img_zip.Download(), IsOk());
  ASSERT_THAT(img_zip.ExtractAll(), IsOk());
  ASSERT_THAT(img_zip.DeleteLocalFile(), IsOk());

  EXPECT_FALSE(FileExists(Root() + "/" + kImgZip));
  EXPECT_TRUE(FileExists(Root() + "/super.img"));
  EXPECT_TRUE(FileExists(Root() + "/nested/file.txt"));
  EXPECT_THAT(fetcher_config_.get_cvd_files(), Contains(Key("super.img")));
}

TEST_F(FetchArtifactDirectoryTest, ExtractOneLinksSingleMember) {
  std::optional<FetchBuildContext> build = context_.DefaultBuild();
  ASSERT_TRUE(build.has_value());
  FetchArtifact img_zip = build->Artifact(kImgZip);

  ASSERT_THAT(img_zip.Download(), IsOk());
  ASSERT_THAT(img_zip.ExtractOneTo("nested/file.txt", "renamed.txt"), IsOk());

  EXPECT_TRUE(FileExists(Root() + "/renamed.txt"));
  EXPECT_FALSE(FileExists(Root() + "/super.img"));
  EXPECT_THAT(fetcher_config_.get_cvd_files(), Contains(Key("renamed.txt")));
}

TEST_F(FetchArtifactDirectoryTest, ExtractOneMissingMemberFails) {
  std::optional<FetchBuildContext> build = context_.DefaultBuild();
  ASSERT_TRUE(build.has_value());
  FetchArtifact img_zip = build->Artifact(kImgZip);

  ASSERT_THAT(img_zip.Download(), IsOk());
  EXPECT_THAT(img_zip.ExtractOne("missing.img"), IsError());
  EXPECT_THAT(img_zip.ExtractOne("nested"), IsError());
}

}  // namespace
}  // namespace cuttlefish
