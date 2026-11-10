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

#include "cuttlefish/host/libs/web/fallback_build_api.h"

#include <string>
#include <utility>
#include <vector>

#include "gmock/gmock.h"
#include "gtest/gtest.h"

#include "cuttlefish/host/libs/web/android_build.h"
#include "cuttlefish/host/libs/web/android_build_string/device_build_string.h"
#include "cuttlefish/host/libs/web/build_api.h"
#include "cuttlefish/host/libs/zip/libzip_cc/seekable_source.h"
#include "cuttlefish/host/libs/zip/libzip_cc/writable_source.h"
#include "cuttlefish/result/result.h"
#include "cuttlefish/result/result_matchers.h"

namespace cuttlefish {
namespace {

using ::testing::ElementsAre;
using ::testing::Field;
using ::testing::HasSubstr;
using ::testing::IsEmpty;

constexpr char kContents[] = "artifact bytes";

// Either serves every request or fails every request, and records which
// requests reached it.
class FakeBackend : public BuildApi<DeviceBuildString, DeviceBuild> {
 public:
  FakeBackend(std::string name, bool succeeds)
      : name_(std::move(name)), succeeds_(succeeds) {}

  Result<DeviceBuild> GetBuild(const DeviceBuildString& build_string) override {
    calls.push_back("GetBuild");
    if (!succeeds_) {
      return CF_ERRF("{} cannot resolve builds", name_);
    }
    return DeviceBuild{.id = name_, .target = *build_string.target};
  }

  Result<std::string> DownloadFile(const DeviceBuild&,
                                   const std::string& target_directory,
                                   const std::string& artifact_name) override {
    calls.push_back("DownloadFile");
    if (!succeeds_) {
      return CF_ERRF("{} cannot download '{}'", name_, artifact_name);
    }
    return name_ + ":" + target_directory + "/" + artifact_name;
  }

  Result<SeekableZipSource> FileReader(
      const DeviceBuild&, const std::string& artifact_name) override {
    calls.push_back("FileReader");
    if (!succeeds_) {
      return CF_ERRF("{} cannot read '{}'", name_, artifact_name);
    }
    return CF_EXPECT(
        WritableZipSource::BorrowData(contents_.data(), contents_.size()));
  }

  std::vector<std::string> calls;

 private:
  std::string name_;
  bool succeeds_;
  std::string contents_ = kContents;
};

DeviceBuildString TestBuildString() {
  return DeviceBuildString{.branch_or_id = "123", .target = "target"};
}

DeviceBuild TestBuild() { return DeviceBuild{.id = "123", .target = "target"}; }

TEST(FallbackBuildApiTest, DownloadFileUsesTheFirstBackendThatSucceeds) {
  FakeBackend first("first", true);
  FakeBackend second("second", true);
  FallbackBuildApi<DeviceBuildString, DeviceBuild> api({&first, &second});

  EXPECT_THAT(api.DownloadFile(TestBuild(), "/dir", "a-img-1.zip"),
              IsOkAndValue("first:/dir/a-img-1.zip"));
  EXPECT_THAT(first.calls, ElementsAre("DownloadFile"));
  EXPECT_THAT(second.calls, IsEmpty());
}

TEST(FallbackBuildApiTest, DownloadFileFallsBackWhenABackendFails) {
  FakeBackend first("first", false);
  FakeBackend second("second", true);
  FallbackBuildApi<DeviceBuildString, DeviceBuild> api({&first, &second});

  EXPECT_THAT(api.DownloadFile(TestBuild(), "/dir", "a-img-1.zip"),
              IsOkAndValue("second:/dir/a-img-1.zip"));
  EXPECT_THAT(first.calls, ElementsAre("DownloadFile"));
  EXPECT_THAT(second.calls, ElementsAre("DownloadFile"));
}

TEST(FallbackBuildApiTest, DownloadFileReturnsTheLastErrorWhenAllFail) {
  FakeBackend first("first", false);
  FakeBackend second("second", false);
  FallbackBuildApi<DeviceBuildString, DeviceBuild> api({&first, &second});

  EXPECT_THAT(api.DownloadFile(TestBuild(), "/dir", "a-img-1.zip"),
              IsErrorAndMessage(HasSubstr("second cannot download")));
  EXPECT_THAT(first.calls, ElementsAre("DownloadFile"));
  EXPECT_THAT(second.calls, ElementsAre("DownloadFile"));
}

TEST(FallbackBuildApiTest, GetBuildFallsBackWhenABackendFails) {
  FakeBackend first("first", false);
  FakeBackend second("second", true);
  FallbackBuildApi<DeviceBuildString, DeviceBuild> api({&first, &second});

  EXPECT_THAT(api.GetBuild(TestBuildString()),
              IsOkAndValue(Field(&DeviceBuild::id, "second")));
  EXPECT_THAT(first.calls, ElementsAre("GetBuild"));
  EXPECT_THAT(second.calls, ElementsAre("GetBuild"));
}

TEST(FallbackBuildApiTest, GetBuildReturnsTheLastErrorWhenAllFail) {
  FakeBackend first("first", false);
  FakeBackend second("second", false);
  FallbackBuildApi<DeviceBuildString, DeviceBuild> api({&first, &second});

  EXPECT_THAT(api.GetBuild(TestBuildString()),
              IsErrorAndMessage(HasSubstr("second cannot resolve builds")));
  EXPECT_THAT(first.calls, ElementsAre("GetBuild"));
  EXPECT_THAT(second.calls, ElementsAre("GetBuild"));
}

TEST(FallbackBuildApiTest, FileReaderFallsBackWhenABackendFails) {
  FakeBackend first("first", false);
  FakeBackend second("second", true);
  FallbackBuildApi<DeviceBuildString, DeviceBuild> api({&first, &second});

  EXPECT_THAT(api.FileReader(TestBuild(), "a.zip"), IsOk());
  EXPECT_THAT(first.calls, ElementsAre("FileReader"));
  EXPECT_THAT(second.calls, ElementsAre("FileReader"));
}

TEST(FallbackBuildApiTest, FileReaderReturnsTheLastErrorWhenAllFail) {
  FakeBackend first("first", false);
  FakeBackend second("second", false);
  FallbackBuildApi<DeviceBuildString, DeviceBuild> api({&first, &second});

  EXPECT_THAT(api.FileReader(TestBuild(), "a.zip"),
              IsErrorAndMessage(HasSubstr("second cannot read")));
  EXPECT_THAT(first.calls, ElementsAre("FileReader"));
  EXPECT_THAT(second.calls, ElementsAre("FileReader"));
}

TEST(FallbackBuildApiTest, SingleBackendIsUsedDirectly) {
  FakeBackend only("only", false);
  FallbackBuildApi<DeviceBuildString, DeviceBuild> api({&only});

  EXPECT_THAT(api.DownloadFile(TestBuild(), "/dir", "a.zip"),
              IsErrorAndMessage(HasSubstr("only cannot download")));
  EXPECT_THAT(only.calls, ElementsAre("DownloadFile"));
}

TEST(FallbackBuildApiTest, NoBackendsIsAnError) {
  FallbackBuildApi<DeviceBuildString, DeviceBuild> api({});

  EXPECT_THAT(api.DownloadFile(TestBuild(), "/dir", "a.zip"),
              IsErrorAndMessage(HasSubstr("No backends")));
}

}  // namespace
}  // namespace cuttlefish
