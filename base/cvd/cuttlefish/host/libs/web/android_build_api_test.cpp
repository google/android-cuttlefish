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

#include "cuttlefish/host/libs/web/android_build_api.h"

#include <string>

#include "gmock/gmock.h"
#include "gtest/gtest.h"

#include "cuttlefish/host/libs/web/android_build.h"
#include "cuttlefish/host/libs/web/android_build_url.h"
#include "cuttlefish/host/libs/web/http_client/fake_http_client.h"
#include "cuttlefish/host/libs/web/http_client/http_client.h"
#include "cuttlefish/host/libs/zip/libzip_cc/seekable_source.h"
#include "cuttlefish/result/result.h"
#include "cuttlefish/result/result_matchers.h"

namespace cuttlefish {
namespace {

TEST(AndroidBuildApiTest, FileReader) {
  FakeHttpClient http_client;
  AndroidBuildUrl build_url("https://androidbuild-pa.googleapis.com/v4", "",
                            "");
  AndroidBuildApi api(http_client, build_url);

  http_client.SetResponse("{\"signedUrl\": \"http://zip-url\"}", "/url");

  HttpResponse<std::string> res = {
      .data = "abc",
      .http_code = 200,
      .headers =
          {
              {"accept-ranges", "bytes"},
              {"content-length", "3"},
          },
  };
  http_client.SetResponse(res, "http://zip-url");

  Build build = DeviceBuild{.id = "123", .target = "test"};
  Result<SeekableZipSource> source = api.FileReader(build, "a.zip");
  ASSERT_THAT(source, IsOk());

  Result<SeekingZipSourceReader> reader = source->Reader();
  ASSERT_THAT(reader, IsOk());

  char buf[4] = {};
  ASSERT_THAT(reader->Read(buf, 3), IsOkAndValue(3));
  EXPECT_STREQ(buf, "abc");

  EXPECT_TRUE(http_client.RequestMade(
      "https://androidbuild-pa.googleapis.com/v4/builds/123/test/attempts/"
      "latest/artifacts/a.zip/url"));
}

}  // namespace
}  // namespace cuttlefish
