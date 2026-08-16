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

#include "cuttlefish/host/libs/web/http_client/scrub_secrets.h"

#include "gtest/gtest.h"

namespace cuttlefish {
namespace http_client {

TEST(HttpClientUtilTest, ScrubSecretsAuthorizationMatch) {
  EXPECT_EQ(ScrubSecrets("Authorization: Bearer 123456"),
            "Authorization: Bearer 123456...");
  EXPECT_EQ(ScrubSecrets("Authorization: Bearer 1234567890"),
            "Authorization: Bearer 123456...");
  EXPECT_EQ(ScrubSecrets("Authorization: Basic 1234567890"),
            "Authorization: Basic 123456...");
  EXPECT_EQ(ScrubSecrets("text\nAuthorization: Bearer 1234567890"),
            "text\nAuthorization: Bearer 123456...");
  EXPECT_EQ(ScrubSecrets("Authorization: Bearer 1234567890\nnext_line"),
            "Authorization: Bearer 123456...\nnext_line");
  EXPECT_EQ(ScrubSecrets("Authorization: Bearer 1234567890 \nnext_line"),
            "Authorization: Bearer 123456... \nnext_line");
  EXPECT_EQ(ScrubSecrets("Authorization: Bearer 1234567890  \nnext_line"),
            "Authorization: Bearer 123456...  \nnext_line");
  EXPECT_EQ(ScrubSecrets("[text] [authorization: Bearer 1234567890]"),
            "[text] [authorization: Bearer 123456...");
}

TEST(HttpClientUtilTest, ScrubSecretsAuthorizationNoMatch) {
  EXPECT_EQ(ScrubSecrets("hello world"), "hello world");
  EXPECT_EQ(ScrubSecrets("Authorization: Bearer 12345"),
            "Authorization: Bearer 12345");
  EXPECT_EQ(ScrubSecrets("Authorization Bearer 1234567890"),
            "Authorization Bearer 1234567890");
  EXPECT_EQ(ScrubSecrets("Authorization: 1234567890"),
            "Authorization: 1234567890");
}

TEST(HttpClientUtilTest, ScrubSecretsClientSecretMatch) {
  EXPECT_EQ(ScrubSecrets("client_secret=123456"), "client_secret=123456...");
  EXPECT_EQ(ScrubSecrets("client_secret=1234567890"),
            "client_secret=123456...");
  EXPECT_EQ(ScrubSecrets("text\nclient_secret=1234567890"),
            "text\nclient_secret=123456...");
  EXPECT_EQ(ScrubSecrets("client_id=abc&client_secret=1234567890"),
            "client_id=abc&client_secret=123456...");
  EXPECT_EQ(ScrubSecrets("client_secret=1234567890\nnext_line"),
            "client_secret=123456...\nnext_line");
  EXPECT_EQ(ScrubSecrets("client_secret=1234567890 \nnext_line"),
            "client_secret=123456... \nnext_line");
  EXPECT_EQ(ScrubSecrets("client_secret=1234567890  \nnext_line"),
            "client_secret=123456...  \nnext_line");
  EXPECT_EQ(ScrubSecrets("client_secret=1234567890&client_id=abc"),
            "client_secret=123456...&client_id=abc");
}

TEST(HttpClientUtilTest, ScrubSecretsClientSecretNoMatch) {
  EXPECT_EQ(ScrubSecrets("hello world"), "hello world");
  EXPECT_EQ(ScrubSecrets("client_secret=12345"), "client_secret=12345");
  EXPECT_EQ(ScrubSecrets("client_id=1234567890"), "client_id=1234567890");
}

TEST(HttpClientUtilTest, ScrubSecretsRequestLineQueryMatch) {
  EXPECT_EQ(ScrubSecrets(
                "GET /bucket/image.zip?X-Goog-Signature=1234567890 HTTP/1.1"),
            "GET /bucket/image.zip?... HTTP/1.1");
  EXPECT_EQ(ScrubSecrets("GET /bucket/image.zip?X-Goog-Signature=1234567890 "
                         "HTTP/1.1\r\nHost: storage.googleapis.com\r\n"),
            "GET /bucket/image.zip?... HTTP/1.1\r\nHost: "
            "storage.googleapis.com\r\n");
  EXPECT_EQ(ScrubSecrets("HEAD https://example.com/image.zip?token=1234567890 "
                         "HTTP/1.1"),
            "HEAD https://example.com/image.zip?... HTTP/1.1");
}

TEST(HttpClientUtilTest, ScrubSecretsSignedRequestHeadersMatch) {
  EXPECT_EQ(
      ScrubSecrets("GET /bucket/image.zip?X-Goog-Signature=1234567890 "
                   "HTTP/1.1\r\nHost: storage.googleapis.com\r\n"
                   "Authorization: Bearer 1234567890\r\nAccept: */*\r\n"),
      "GET /bucket/image.zip?... HTTP/1.1\r\nHost: storage.googleapis.com\r\n"
      "Authorization: Bearer 123456...\r\nAccept: */*\r\n");
}

TEST(HttpClientUtilTest, ScrubSecretsRedirectLocationMatch) {
  EXPECT_EQ(ScrubSecrets("Location: https://storage.googleapis.com/bucket/"
                         "image.zip?X-Goog-Signature=1234567890"),
            "Location: https://storage.googleapis.com/bucket/image.zip?...");
  EXPECT_EQ(ScrubSecrets("HTTP/1.1 302 Found\r\nLocation: "
                         "https://example.com/a.zip?token=1234567890\r\n"),
            "HTTP/1.1 302 Found\r\nLocation: "
            "https://example.com/a.zip?...\r\n");
}

TEST(HttpClientUtilTest, ScrubSecretsQueryWithoutRequestLineSuffix) {
  EXPECT_EQ(ScrubSecrets("GET /bucket/image.zip?X-Goog-Signature=1234567890"),
            "GET /bucket/image.zip?...");
  EXPECT_EQ(
      ScrubSecrets("GET /bucket/image.zip?X-Goog-Signature=1234567890\r\n"),
      "GET /bucket/image.zip?...\r\n");
}

TEST(HttpClientUtilTest, ScrubSecretsJsonSignedUrlMatch) {
  EXPECT_EQ(
      ScrubSecrets(
          "{\n   \"signedUrl\" : "
          "\"https://storage.googleapis.com/a.zip?X-Goog-Sig=123\"\n}"),
      "{\n   \"signedUrl\" : \"https://storage.googleapis.com/a.zip?...\"\n}");
  EXPECT_EQ(
      ScrubSecrets("{\n   \"a\" : \"x?tok=123\",\n   \"b\" : \"plain\"\n}"),
      "{\n   \"a\" : \"x?...\",\n   \"b\" : \"plain\"\n}");
}

TEST(HttpClientUtilTest, ScrubSecretsRequestLineQueryNoMatch) {
  EXPECT_EQ(ScrubSecrets("GET /bucket/image.zip HTTP/1.1"),
            "GET /bucket/image.zip HTTP/1.1");
  EXPECT_EQ(ScrubSecrets("Host: example.com"), "Host: example.com");
}

TEST(HttpClientUtilTest, ScrubUrlRemovesQueryAndFragment) {
  EXPECT_EQ(
      ScrubUrl("https://example.com/image.zip?X-Goog-Signature=1234567890"),
      "https://example.com/image.zip");
  EXPECT_EQ(ScrubUrl("https://example.com/image.zip#sha256=1234567890"),
            "https://example.com/image.zip");
  EXPECT_EQ(ScrubUrl("https://example.com/image.zip?a=1#b"),
            "https://example.com/image.zip");
}

TEST(HttpClientUtilTest, ScrubUrlKeepsPlainUrl) {
  EXPECT_EQ(ScrubUrl("https://example.com/image.zip"),
            "https://example.com/image.zip");
  EXPECT_EQ(ScrubUrl(""), "");
}

}  // namespace http_client
}  // namespace cuttlefish
