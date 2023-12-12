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

#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "common/libs/utils/base64.h"

namespace cuttlefish {

TEST(Base64Test, EncodeMult3) {
  std::string in = "foobar";
  std::string expected("Zm9vYmFy");
  std::string out;
  ASSERT_TRUE(EncodeBase64(in.c_str(), in.size(), &out));
  ASSERT_EQ(out.size(), expected.size());
  ASSERT_EQ(out, expected);
}

TEST(Base64Test, EncodeNonMult3) {
  std::string in = "foobar1";
  std::string expected("Zm9vYmFyMQ==");
  std::string out;
  ASSERT_TRUE(EncodeBase64(in.c_str(), in.size(), &out));
   ASSERT_EQ(out.size(), expected.size());
  ASSERT_EQ(out, expected);
}

TEST(Base64Test, DecodeMult3) {
  std::string in = "Zm9vYmFy";
  std::vector<uint8_t> expected{'f','o','o','b','a','r'};
  std::vector<uint8_t> out;
  ASSERT_TRUE(DecodeBase64(in, &out));
  ASSERT_EQ(out.size(), expected.size());
  ASSERT_EQ(out, expected);
}

TEST(Base64Test, DecodeNonMult3) {
  std::string in = "Zm9vYmFyMQ==";
  std::vector<uint8_t> expected{'f','o','o','b','a','r','1'};
  std::vector<uint8_t> out;
  ASSERT_TRUE(DecodeBase64(in, &out));
  ASSERT_EQ(out.size(), expected.size());
  ASSERT_EQ(out, expected);
}


}
