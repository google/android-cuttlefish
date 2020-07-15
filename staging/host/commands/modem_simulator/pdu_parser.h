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

#pragma once

#include <string>

namespace cuttlefish {

class PDUParser {
 public:
  explicit PDUParser(std::string &pdu);
  ~PDUParser() = default;

  bool IsValidPDU();
  bool IsNeededStatuReport();
  std::string CreatePDU();
  std::string CreateRemotePDU(std::string& host_port);
  std::string CreateStatuReport(int message_reference);
  std::string GetPhoneNumberFromAddress();

  static std::string BCDToString(std::string& data);

 private:
  bool DecodePDU(std::string& pdu);
  int Hex2ToByte(const std::string& hex);
  int HexCharToInt(char c);
  std::string IntToHexString(int value);
  std::string GetCurrentTimeStamp();

  bool is_valid_pdu_;

  // Ignore SMSC address, default to be "00" when create PDU
  std::string pdu_type_;
  std::string message_reference_;
  std::string originator_address_;
  std::string protocol_id_;
  std::string data_code_scheme_;
  std::string user_data_;
};

} // namespace cuttlefish
