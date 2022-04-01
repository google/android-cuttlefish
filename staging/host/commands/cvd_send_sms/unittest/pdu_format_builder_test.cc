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
  builder.SetSenderNumber("+16501234567");

  std::string result = builder.Build();

  EXPECT_EQ(result, "");
}

TEST(PDUFormatBuilderTest, With161CharactersFails) {
  PDUFormatBuilder builder;
  builder.SetUserData(
      "cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc"
      "cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc"
      "ccccccccccccccccccccccccccccccccccccccccc");
  builder.SetSenderNumber("+16501234567");

  std::string result = builder.Build();

  EXPECT_EQ(result, "");
}

TEST(PDUFormatBuilderTest, With1CharacterSucceeds) {
  PDUFormatBuilder builder;
  builder.SetUserData("c");
  builder.SetSenderNumber("+16501234567");

  std::string result = builder.Build();

  EXPECT_EQ(result, "0001000b916105214365f700000163");
}

TEST(PDUFormatBuilderTest, With7CharactersSucceeds) {
  PDUFormatBuilder builder;
  builder.SetUserData("ccccccc");
  builder.SetSenderNumber("+16501234567");

  std::string result = builder.Build();

  EXPECT_EQ(result, "0001000b916105214365f7000007e3f1783c1e8f01");
}

TEST(PDUFormatBuilderTest, With8CharactersSucceeds) {
  PDUFormatBuilder builder;
  builder.SetUserData("cccccccc");
  builder.SetSenderNumber("+16501234567");

  std::string result = builder.Build();

  EXPECT_EQ(result, "0001000b916105214365f7000008e3f1783c1e8fc7");
}

TEST(PDUFormatBuilderTest, With160CharactersSucceeds) {
  PDUFormatBuilder builder;
  builder.SetUserData(
      "cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc"
      "cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc"
      "cccccccccccccccccccccccccccccccccccccccc");
  builder.SetSenderNumber("+16501234567");

  std::string result = builder.Build();

  EXPECT_EQ(result,
            "0001000b916105214365f70000a0"
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
  builder.SetSenderNumber("+16501234567");

  std::string result = builder.Build();

  EXPECT_EQ(result,
            "0001000b916105214365f70000a0"
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
  builder.SetSenderNumber("+16501234567");

  std::string result = builder.Build();

  EXPECT_EQ(
      result,
      "0001000b916105214365f70000808080604028180e888462c168381e90886442a9582e98"
      "8c66c3e9783ea09068442a994ea8946ac56ab95eb0986c46abd96eb89c6ec7ebf97ec0a0"
      "70482c1a8fc8a472c96c3a9fd0a8744aad5aafd8ac76cbed7abfe0b0784c2e9bcfe8b47a"
      "cd6ebbdff0b87c4eafdbeff8bc7ecfeffbff");
}

TEST(PDUFormatBuilderTest, WithEmptySenderPhoneNumberFails) {
  PDUFormatBuilder builder;
  builder.SetUserData("c");
  builder.SetSenderNumber("");

  std::string result = builder.Build();

  EXPECT_EQ(result, "");
}

TEST(PDUFormatBuilderTest, WithInvalidSenderPhoneNumberFails) {
  std::vector<std::string> numbers{"06501234567", "1", "1650603619399999"};
  PDUFormatBuilder builder;
  builder.SetUserData("c");

  for (auto n : numbers) {
    builder.SetSenderNumber(n);
    EXPECT_EQ(builder.Build(), "");
  }
}

TEST(PDUFormatBuilderTest, WithoutLeadingPlusSignSucceeds) {
  PDUFormatBuilder builder;
  builder.SetUserData("c");
  builder.SetSenderNumber("16501234567");

  std::string result = builder.Build();

  EXPECT_EQ(result, "0001000b916105214365f700000163");
}

TEST(PDUFormatBuilderTest, WithOddSenderPhoneNumberLengthSucceeds) {
  PDUFormatBuilder builder;
  builder.SetUserData("c");
  builder.SetSenderNumber("+16501234567");

  std::string result = builder.Build();

  EXPECT_EQ(result, "0001000b916105214365f700000163");
}

TEST(PDUFormatBuilderTest, WithEvenSenderPhoneNumberLengthSucceeds) {
  PDUFormatBuilder builder;
  builder.SetUserData("c");
  builder.SetSenderNumber("+526501234567");

  std::string result = builder.Build();

  EXPECT_EQ(result, "0001000c9125561032547600000163");
}

}  // namespace
}  // namespace cuttlefish
