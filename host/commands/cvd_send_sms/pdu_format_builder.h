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

#pragma once

#include <string>

namespace cuttlefish {

// Builds PDU format strings used to send SMS to Cuttlefish modem simulator.
//
// PDU format is specified by the Etsi organization in GSM 03.40
// https://www.etsi.org/deliver/etsi_gts/03/0340/05.03.00_60/gsmts_0340v050300p.pdf
//
// The resulting PDU format string encapsulates different parameters
// values like:
// * The phone number.
// * Data coding scheme. 7 bit Alphabet or 8 bit (used in e.g. smart
// messaging, OTA provisioning etc)
// * User data.
//
// NOTE: For sender phone number, only international numbers following the
// E.164 format (https://www.itu.int/rec/T-REC-E.164) are supported.
//
// NOTE: The coding scheme is not parameterized yet using always the 7bit
// Alphabet coding scheme.
class PDUFormatBuilder {
 public:
  void SetUserData(const std::string& user_data);
  void SetSenderNumber(const std::string& number);
  // Returns the corresponding PDU format string, returns an empty string if
  // the User Data or the Sender Number set are invalid.
  std::string Build();

 private:
  std::string user_data_;
  std::string sender_number_;
};

}  // namespace cuttlefish
