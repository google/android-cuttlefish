//
// Copyright (C) 2020 The Android Open Source Project
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

#include <gtest/gtest.h>

#include "modem_simulator/command_parser.h"

TEST(CommandParserUnitTest, SkipPrefix) {
  std::string command = "AT+SPUSATENVECMD=\"D3078202018190014E\"";

  cuttlefish::CommandParser cmd(command);
  cmd.SkipPrefix();
  std::string result(*cmd);
  ASSERT_STREQ("\"D3078202018190014E\"", result.c_str());
}

TEST(CommandParserUnitTest, SkipPrefixAT) {
  std::string command = "AT+SPUSATENVECMD=\"D3078202018190014E\"";

  cuttlefish::CommandParser cmd(command);
  cmd.SkipPrefixAT();
  std::string result(*cmd);
  ASSERT_STREQ("+SPUSATENVECMD=\"D3078202018190014E\"", result.c_str());
}

TEST(CommandParserUnitTest, SkipComma) {
  std::string command = "+COPS: 0,1,\"CMCC\",7";

  cuttlefish::CommandParser cmd(command);
  cmd.SkipComma();
  std::string result(*cmd);
  ASSERT_STREQ("1,\"CMCC\",7", result.c_str());
}

TEST(CommandParserUnitTest, SkipWhiteSpace) {
  std::string command = "+COPS: 0,1,\"CMCC\",7";
  cuttlefish::CommandParser cmd(command);

  cmd.GetNextStr(':');
  cmd.SkipWhiteSpace();
  std::string result(*cmd);
  ASSERT_STREQ("0,1,\"CMCC\",7", result.c_str());
}

TEST(CommandParserUnitTest, GetNextStr_default) {
  std::string command = "+COPS: 0,1,\"CMCC\",7";

  cuttlefish::CommandParser cmd(command);
  std::string result(cmd.GetNextStr());
  ASSERT_STREQ("CMCC", result.c_str());
}

TEST(CommandParserUnitTest, GetNextStr_withparam) {
  std::string command = "+COPS: 0,1,\"CMCC\",7";

  cuttlefish::CommandParser cmd(command);
  std::string result(cmd.GetNextStr(','));
  ASSERT_STREQ("+COPS: 0", result.c_str());

  std::string result2(cmd.GetNextStr(';'));
  ASSERT_STREQ("1,\"CMCC\",7", result2.c_str());
}

TEST(CommandParserUnitTest, GetNextInt) {
  std::string command = "AT+CRSM=192,28421,0,0,15,0,\"3F007FFF\"";

  cuttlefish::CommandParser cmd(command);
  cmd.SkipPrefix();  // skip "AT+CRSM="
  ASSERT_EQ(192, cmd.GetNextInt());
  ASSERT_EQ(28421, cmd.GetNextInt());
}

TEST(CommandParserUnitTest, GetNextHexInt) {  // Hexadecimal string to decimal value
  std::string command = "C0,6F05";

  cuttlefish::CommandParser cmd(command);
  ASSERT_EQ(192, cmd.GetNextHexInt());
  ASSERT_EQ(28421, cmd.GetNextHexInt());
}

TEST(CommandParserUnitTest, GetNextStrDeciToHex) {
  std::string command = "AT+CRSM=192,28421,0,0,15,0,\"3F007FFF\"";

  cuttlefish::CommandParser cmd(command);
  cmd.SkipPrefix();
  std::string result(cmd.GetNextStrDeciToHex());
  ASSERT_STREQ("C0", result.c_str());  // 192

  std::string result2(cmd.GetNextStrDeciToHex());
  ASSERT_STREQ("6F05", result2.c_str());  // 28421
}
