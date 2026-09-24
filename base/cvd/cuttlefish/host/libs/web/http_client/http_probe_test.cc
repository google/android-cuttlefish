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

#include "cuttlefish/host/libs/web/http_client/http_probe.h"

#include <optional>
#include <string>
#include <vector>

#include "gmock/gmock.h"
#include "gtest/gtest.h"

#include "cuttlefish/host/libs/web/http_client/fake_http_client.h"
#include "cuttlefish/host/libs/web/http_client/http_client.h"
#include "cuttlefish/result/result.h"
#include "cuttlefish/result/result_matchers.h"

namespace cuttlefish {
namespace {

using ::testing::AllOf;
using ::testing::Contains;
using ::testing::ElementsAre;
using ::testing::HasSubstr;
using ::testing::Not;

constexpr char kUrl[] = "https://example.com/dist/phone-img-1.zip";
constexpr char kSignedUrl[] =
    "https://example.com/dist/phone-img-1.zip?X-Goog-Signature=secret";

TEST(ProbeHttpObjectTests, HeadReportsTheObjectSuccess) {
  FakeHttpClient http_client;
  std::vector<HttpMethod> methods;
  http_client.SetResponse(
      [&methods](const HttpRequest& request) {
        methods.push_back(request.method);
        return HttpResponse<std::string>{
            .http_code = 200,
            .headers = {{"etag", "\"v1\""},
                        {"accept-ranges", "bytes"},
                        {"content-length", "4096"}},
        };
      },
      kUrl);

  Result<HttpObjectInfo> info = ProbeHttpObject(http_client, kUrl, {});

  ASSERT_THAT(info, IsOk());
  EXPECT_EQ(info->etag, "\"v1\"");
  EXPECT_TRUE(info->accept_ranges);
  EXPECT_EQ(info->size, 4096);
  EXPECT_THAT(methods, ElementsAre(HttpMethod::kHead));
}

TEST(ProbeHttpObjectTests, RefusedHeadFallsBackToARangedGetSuccess) {
  FakeHttpClient http_client;
  std::vector<std::string> get_headers;
  http_client.SetResponse(
      [&get_headers](const HttpRequest& request) {
        if (request.method == HttpMethod::kHead) {
          return HttpResponse<std::string>{.http_code = 403};
        }
        get_headers = request.headers;
        return HttpResponse<std::string>{
            .data = "a",
            .http_code = 206,
            .headers = {{"etag", "\"v1\""},
                        {"content-range", "bytes 0-0/4096"}},
        };
      },
      kUrl);

  Result<HttpObjectInfo> info = ProbeHttpObject(http_client, kSignedUrl, {});

  ASSERT_THAT(info, IsOk());
  EXPECT_EQ(info->etag, "\"v1\"");
  EXPECT_TRUE(info->accept_ranges);
  EXPECT_EQ(info->size, 4096);
  EXPECT_THAT(get_headers, Contains("Range: bytes=0-0"));
}

TEST(ProbeHttpObjectTests, WithoutRangesSuccess) {
  FakeHttpClient http_client;
  http_client.SetResponse(
      HttpResponse<std::string>{.http_code = 200,
                                .headers = {{"content-length", "4096"}}},
      kUrl);

  Result<HttpObjectInfo> info = ProbeHttpObject(http_client, kUrl, {});

  ASSERT_THAT(info, IsOk());
  EXPECT_FALSE(info->accept_ranges);
  EXPECT_EQ(info->size, 4096);
  EXPECT_EQ(info->etag, std::nullopt);
}

TEST(ProbeHttpObjectTests, PassesHeadersThroughSuccess) {
  FakeHttpClient http_client;
  std::vector<std::string> seen_headers;
  http_client.SetResponse(
      [&seen_headers](const HttpRequest& request) {
        seen_headers = request.headers;
        return HttpResponse<std::string>{.http_code = 200};
      },
      kUrl);

  ASSERT_THAT(ProbeHttpObject(http_client, kUrl, {"Authorization: Bearer t"}),
              IsOk());
  EXPECT_THAT(seen_headers, ElementsAre("Authorization: Bearer t"));
}

TEST(ProbeHttpObjectTests, MissingObjectFail) {
  FakeHttpClient http_client;
  http_client.SetResponse(HttpResponse<std::string>{.http_code = 404}, kUrl);

  EXPECT_THAT(ProbeHttpObject(http_client, kSignedUrl, {}),
              IsErrorAndMessage(AllOf(HasSubstr(kUrl), HasSubstr("404"),
                                      Not(HasSubstr("secret")))));
}

TEST(ProbeHttpObjectTests, RedirectFail) {
  FakeHttpClient http_client;
  http_client.SetResponse(HttpResponse<std::string>{.http_code = 302}, kUrl);

  EXPECT_THAT(
      ProbeHttpObject(http_client, kUrl, {}),
      IsErrorAndMessage(AllOf(HasSubstr("302"), HasSubstr("redirect"))));
}

TEST(ProbeHttpObjectTests, ForbiddenGetAfterForbiddenHeadFail) {
  FakeHttpClient http_client;
  std::vector<HttpMethod> methods;
  http_client.SetResponse(
      [&methods](const HttpRequest& request) {
        methods.push_back(request.method);
        return HttpResponse<std::string>{.http_code = 403};
      },
      kUrl);

  EXPECT_THAT(ProbeHttpObject(http_client, kUrl, {}),
              IsErrorAndMessage(HasSubstr("403")));
  EXPECT_THAT(methods, ElementsAre(HttpMethod::kHead, HttpMethod::kGet));
}

}  // namespace
}  // namespace cuttlefish
