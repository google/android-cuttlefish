//
// Copyright (C) 2022 The Android Open Source Project
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

#include "host/commands/cvd_send_sms/pdu_format_builder.h"

#include <gtest/gtest.h>

namespace cuttlefish {
namespace {

TEST(PDUFormatBuilderTest, EmptyUserDataFails) {
  PDUFormatBuilder builder;

  std::string result = builder.Build();

  EXPECT_EQ(result, "");
}

TEST(PDUFormatBuilderTest, NotInAlphabetCharacterFails) {
  PDUFormatBuilder builder;
  builder.SetUserData("ccccccc☺");

  std::string result = builder.Build();

  EXPECT_EQ(result, "");
}

TEST(PDUFormatBuilderTest, With161CharactersFails) {
  PDUFormatBuilder builder;
  builder.SetUserData(
      "cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc"
      "cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc"
      "ccccccccccccccccccccccccccccccccccccccccc");

  std::string result = builder.Build();

  EXPECT_EQ(result, "");
}

TEST(PDUFormatBuilderTest, With1CharacterSucceeds) {
  PDUFormatBuilder builder;
  builder.SetUserData("c");

  std::string result = builder.Build();

  EXPECT_EQ(result, "0001000B916105214365F700000163");
}

TEST(PDUFormatBuilderTest, With7CharactersSucceeds) {
  PDUFormatBuilder builder;
  builder.SetUserData("ccccccc");

  std::string result = builder.Build();

  EXPECT_EQ(result, "0001000B916105214365F7000007e3f1783c1e8f01");
}

TEST(PDUFormatBuilderTest, With8CharactersSucceeds) {
  PDUFormatBuilder builder;
  builder.SetUserData("cccccccc");

  std::string result = builder.Build();

  EXPECT_EQ(result, "0001000B916105214365F7000008e3f1783c1e8fc7");
}

TEST(PDUFormatBuilderTest, With160CharactersSucceeds) {
  PDUFormatBuilder builder;
  builder.SetUserData(
      "cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc"
      "cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc"
      "cccccccccccccccccccccccccccccccccccccccc");

  std::string result = builder.Build();

  EXPECT_EQ(result,
            "0001000B916105214365F70000a0"
            "e3f1783c1e8fc7"
            "e3f1783c1e8fc7"
            "e3f1783c1e8fc7"
            "e3f1783c1e8fc7"
            "e3f1783c1e8fc7"
            "e3f1783c1e8fc7"
            "e3f1783c1e8fc7"
            "e3f1783c1e8fc7"
            "e3f1783c1e8fc7"
            "e3f1783c1e8fc7"
            "e3f1783c1e8fc7"
            "e3f1783c1e8fc7"
            "e3f1783c1e8fc7"
            "e3f1783c1e8fc7"
            "e3f1783c1e8fc7"
            "e3f1783c1e8fc7"
            "e3f1783c1e8fc7"
            "e3f1783c1e8fc7"
            "e3f1783c1e8fc7"
            "e3f1783c1e8fc7");
}

TEST(PDUFormatBuilderTest, With160MultiByteCharactersSucceeds) {
  PDUFormatBuilder builder;
  builder.SetUserData(
      "ΩΩΩΩΩΩΩΩΩΩΩΩΩΩΩΩΩΩΩΩΩΩΩΩΩΩΩΩΩΩΩΩΩΩΩΩΩΩΩΩΩΩΩΩΩΩΩΩΩΩΩΩΩΩΩΩΩΩΩΩ"
      "ΩΩΩΩΩΩΩΩΩΩΩΩΩΩΩΩΩΩΩΩΩΩΩΩΩΩΩΩΩΩΩΩΩΩΩΩΩΩΩΩΩΩΩΩΩΩΩΩΩΩΩΩΩΩΩΩΩΩΩΩ"
      "ΩΩΩΩΩΩΩΩΩΩΩΩΩΩΩΩΩΩΩΩΩΩΩΩΩΩΩΩΩΩΩΩΩΩΩΩΩΩΩΩ");

  std::string result = builder.Build();

  EXPECT_EQ(result,
            "0001000B916105214365F70000a0"
            "954aa552a9542a"
            "954aa552a9542a"
            "954aa552a9542a"
            "954aa552a9542a"
            "954aa552a9542a"
            "954aa552a9542a"
            "954aa552a9542a"
            "954aa552a9542a"
            "954aa552a9542a"
            "954aa552a9542a"
            "954aa552a9542a"
            "954aa552a9542a"
            "954aa552a9542a"
            "954aa552a9542a"
            "954aa552a9542a"
            "954aa552a9542a"
            "954aa552a9542a"
            "954aa552a9542a"
            "954aa552a9542a"
            "954aa552a9542a");
}

TEST(PDUFormatBuilderTest, FullAlphabetSucceeds) {
  PDUFormatBuilder builder;
  builder.SetUserData(
      "@£$¥èéùìòÇ\nØø\rÅåΔ_ΦΓΛΩΠΨΣΘΞ\uffffÆæßÉ "
      "!\"#¤%&'()*+,-./"
      "0123456789:;<=>?"
      "¡ABCDEFGHIJKLMNOPQRSTUVWXYZÄÖÑÜ§¿abcdefghijklmnopqrstuvwxyzäöñüà");

  std::string result = builder.Build();

  EXPECT_EQ(
      result,
      "0001000B916105214365F70000808080604028180e888462c168381e90886442a9582e98"
      "8c66c3e9783ea09068442a994ea8946ac56ab95eb0986c46abd96eb89c6ec7ebf97ec0a0"
      "70482c1a8fc8a472c96c3a9fd0a8744aad5aafd8ac76cbed7abfe0b0784c2e9bcfe8b47a"
      "cd6ebbdff0b87c4eafdbeff8bc7ecfeffbff");
}

}  // namespace
}  // namespace cuttlefish
