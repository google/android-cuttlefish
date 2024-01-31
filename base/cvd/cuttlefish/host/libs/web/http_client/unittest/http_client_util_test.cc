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

#include "host/libs/web/http_client/http_client_util.h"

#include <gtest/gtest.h>

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

}  // namespace http_client
}  // namespace cuttlefish
