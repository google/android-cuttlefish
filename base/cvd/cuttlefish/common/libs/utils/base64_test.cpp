/*
 * Copyright (C) 2023 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "cuttlefish/common/libs/utils/base64.h"

#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "cuttlefish/result/result_matchers.h"
#include "cuttlefish/result/result_type.h"

namespace cuttlefish {

TEST(Base64Test, EncodeMult3) {
  ASSERT_THAT(EncodeBase64("foobar"), IsOkAndValue("Zm9vYmFy"));
}

TEST(Base64Test, EncodeNonMult3) {
  ASSERT_THAT(EncodeBase64("foobar1"), IsOkAndValue("Zm9vYmFyMQ=="));
}

TEST(Base64Test, DecodeMult3) {
  std::vector<uint8_t> expected{'f','o','o','b','a','r'};
  ASSERT_THAT(DecodeBase64("Zm9vYmFy"), IsOkAndValue(expected));
}

TEST(Base64Test, DecodeNonMult3) {
  std::vector<uint8_t> expected{'f','o','o','b','a','r','1'};
  ASSERT_THAT(DecodeBase64("Zm9vYmFyMQ=="), IsOkAndValue(expected));
}

TEST(Base64Test, EncodeOneZero) {
  std::vector<uint8_t> in = {0};

  Result<std::string> string_encoding = EncodeBase64(in.data(), in.size());
  ASSERT_THAT(string_encoding, IsOk());

  ASSERT_THAT(DecodeBase64(*string_encoding), IsOkAndValue(in));
}

TEST(Base64Test, EncodeTwoZeroes) {
  std::vector<uint8_t> in = {0, 0};

  Result<std::string> string_encoding = EncodeBase64(in.data(), in.size());
  ASSERT_THAT(string_encoding, IsOk());

  ASSERT_THAT(DecodeBase64(*string_encoding), IsOkAndValue(in));
}

TEST(Base64Test, EncodeThreeZeroes) {
  std::vector<uint8_t> in = {0, 0, 0};

  Result<std::string> string_encoding = EncodeBase64(in.data(), in.size());
  ASSERT_THAT(string_encoding, IsOk());

  ASSERT_THAT(DecodeBase64(*string_encoding), IsOkAndValue(in));
}
}
