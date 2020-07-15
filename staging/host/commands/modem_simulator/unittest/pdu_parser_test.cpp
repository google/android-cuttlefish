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

#include "modem_simulator/pdu_parser.h"

TEST(PDUParserTest, IsValidPDU_true) {
  std::string pdu = "0001000D91688118109844F0000017AFD7903AB55A9BBA69D639D4ADCBF99E3DCCAE9701";
  cuttlefish::PDUParser smspdu(pdu);
  EXPECT_TRUE(smspdu.IsValidPDU());
}

TEST(PDUParserTest, IsValidPDU_false) {
  std::string pdu = "000100fD91688118109844F0000017AFD7903AB55A9BBA69D639D4ADCBF99E3DCCAE9701";
  cuttlefish::PDUParser smspdu(pdu);
  EXPECT_FALSE(smspdu.IsValidPDU());
}

TEST(PDUParserTest, CreatePDU) {
  std::string pdu = "0001000D91688118109844F0000017AFD7903AB55A9BBA69D639D4ADCBF99E3DCCAE9701";
  cuttlefish::PDUParser smspdu(pdu);
  EXPECT_TRUE(smspdu.IsValidPDU());
  std::string new_pdu = smspdu.CreatePDU();
  const char *expect = "";
  ASSERT_STRNE(new_pdu.c_str(), expect);
}

TEST(PDUParserTest, GetPhoneNumberFromAddress) {
  std::string pdu = "0001000D91688118109844F0000017AFD7903AB55A9BBA69D639D4ADCBF99E3DCCAE9701";
  cuttlefish::PDUParser smspdu(pdu);
  EXPECT_TRUE(smspdu.IsValidPDU());
  std::string phone_number = smspdu.GetPhoneNumberFromAddress();
  const char *expect = "18810189440";
  ASSERT_STREQ(phone_number.c_str(), expect);
}

TEST(PDUParserTest, BCDToString) {
  std::string value = "12345678";
  std::string process_value = cuttlefish::PDUParser::BCDToString(value);
  const char *expect = "21436587";
  ASSERT_STREQ(process_value.c_str(), expect);
}
