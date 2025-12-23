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

#include "cuttlefish/host/libs/web/http_client/fake_http_client.h"

#include <gmock/gmock-matchers.h>
#include <gtest/gtest.h>

#include "cuttlefish/host/libs/web/http_client/http_client.h"
#include "cuttlefish/host/libs/web/http_client/http_string.h"
#include "cuttlefish/result/result.h"
#include "cuttlefish/result/result_matchers.h"

namespace cuttlefish {

TEST(FakeHttpClientTest, WithoutUrlMatching) {
  FakeHttpClient http_client;

  http_client.SetResponse("abc");

  Result<HttpResponse<std::string>> res =
      HttpGetToString(http_client, "https://www.google.com");

  ASSERT_THAT(res, IsOk());
  EXPECT_TRUE(res->HttpSuccess());
  EXPECT_EQ(res->data, "abc");
}

TEST(FakeHttpClientTest, NoMatchingUrl) {
  FakeHttpClient http_client;

  Result<HttpResponse<std::string>> res =
      HttpGetToString(http_client, "https://www.google.com");

  ASSERT_THAT(res, IsOk());
  EXPECT_TRUE(res->HttpClientError());
}

TEST(FakeHttpClientTest, ChoosesUrl) {
  FakeHttpClient http_client;

  http_client.SetResponse("abc", "https://www.google.com");
  http_client.SetResponse("def", "https://www.google.com/path");

  Result<HttpResponse<std::string>> broad =
      HttpGetToString(http_client, "https://www.google.com/other/");
  Result<HttpResponse<std::string>> narrow =
      HttpGetToString(http_client, "https://www.google.com/path/");

  ASSERT_THAT(broad, IsOk());
  ASSERT_THAT(narrow, IsOk());

  EXPECT_TRUE(broad->HttpSuccess());
  EXPECT_EQ(broad->data, "abc");

  EXPECT_TRUE(narrow->HttpSuccess());
  EXPECT_EQ(narrow->data, "def");
}

TEST(FakeHttpClientTest, InvokesCallback) {
  FakeHttpClient http_client;

  http_client.SetResponse([](const HttpRequest& req) {
    return HttpResponse<std::string>{.data = req.url, .http_code = 200};
  });

  Result<HttpResponse<std::string>> res =
      HttpGetToString(http_client, "https://www.google.com");

  ASSERT_THAT(res, IsOk());
  EXPECT_TRUE(res->HttpSuccess());
  EXPECT_EQ(res->data, "https://www.google.com");
}

}  // namespace cuttlefish
