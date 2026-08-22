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

#include "cuttlefish/host/libs/web/composite_build_api.h"

#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "gmock/gmock.h"
#include "gtest/gtest.h"

#include "cuttlefish/host/libs/web/android_build.h"
#include "cuttlefish/host/libs/web/android_build_string.h"
#include "cuttlefish/host/libs/web/build_api.h"
#include "cuttlefish/host/libs/zip/libzip_cc/seekable_source.h"
#include "cuttlefish/result/result.h"
#include "cuttlefish/result/result_matchers.h"

namespace cuttlefish {
namespace {

using ::testing::HasSubstr;

// Every call fails naming the delegate it reached, which is what the routing
// assertions read.
class NamedBuildApi : public BuildApi {
 public:
  explicit NamedBuildApi(std::string name) : name_(std::move(name)) {}

  Result<Build> GetBuild(const BuildString&) override {
    return CF_ERRF("reached '{}'", name_);
  }

  Result<std::string> DownloadFile(const Build&, const std::string&,
                                   const std::string&) override {
    return CF_ERRF("reached '{}'", name_);
  }

  Result<SeekableZipSource> FileReader(const Build&,
                                       const std::string&) override {
    return CF_ERRF("reached '{}'", name_);
  }

 private:
  std::string name_;
};

class CompositeBuildApiTests : public ::testing::Test {
 protected:
  NamedBuildApi android_{"android"};
  NamedBuildApi gcs_{"gcs"};
  NamedBuildApi http_{"http"};
  CompositeBuildApi api_{android_, gcs_, http_};
  DirectoryBuild directory_build_{std::vector<std::string>{"/tmp"}, "target",
                                  std::nullopt};
};

TEST_F(CompositeBuildApiTests, GetBuildRoutesByStringAlternative) {
  EXPECT_THAT(api_.GetBuild(DeviceBuildString{.branch_or_id = "aosp-main"}),
              IsErrorAndMessage(HasSubstr("'android'")));
  EXPECT_THAT(api_.GetBuild(DirectoryBuildString{
                  .paths = std::vector<std::string>{"/tmp"}}),
              IsErrorAndMessage(HasSubstr("'android'")));
  EXPECT_THAT(api_.GetBuild(GcsBuildString{.url = "gs://bucket/dist/"}),
              IsErrorAndMessage(HasSubstr("'gcs'")));
  EXPECT_THAT(api_.GetBuild(HttpBuildString{.url = "https://host/dist/"}),
              IsErrorAndMessage(HasSubstr("'http'")));
}

TEST_F(CompositeBuildApiTests, DownloadFileRoutesByBuildAlternative) {
  EXPECT_THAT(api_.DownloadFile(DeviceBuild{.id = "1"}, "/tmp", "a.txt"),
              IsErrorAndMessage(HasSubstr("'android'")));
  EXPECT_THAT(api_.DownloadFile(directory_build_, "/tmp", "a.txt"),
              IsErrorAndMessage(HasSubstr("'android'")));
  EXPECT_THAT(api_.DownloadFile(GcsBuild{.bucket = "bucket"}, "/tmp", "a.txt"),
              IsErrorAndMessage(HasSubstr("'gcs'")));
  EXPECT_THAT(api_.DownloadFile(HttpBuild{.base = "https://host/dist/"}, "/tmp",
                                "a.txt"),
              IsErrorAndMessage(HasSubstr("'http'")));
}

TEST_F(CompositeBuildApiTests, FileReaderRoutesByBuildAlternative) {
  EXPECT_THAT(api_.FileReader(DeviceBuild{.id = "1"}, "a.txt"),
              IsErrorAndMessage(HasSubstr("'android'")));
  EXPECT_THAT(api_.FileReader(directory_build_, "a.txt"),
              IsErrorAndMessage(HasSubstr("'android'")));
  EXPECT_THAT(api_.FileReader(GcsBuild{.bucket = "bucket"}, "a.txt"),
              IsErrorAndMessage(HasSubstr("'gcs'")));
  EXPECT_THAT(api_.FileReader(HttpBuild{.base = "https://host/dist/"}, "a.txt"),
              IsErrorAndMessage(HasSubstr("'http'")));
}

}  // namespace
}  // namespace cuttlefish
