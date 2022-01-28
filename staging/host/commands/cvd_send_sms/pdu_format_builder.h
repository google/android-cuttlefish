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
// NOTE: For now, this implementation allows to pass the user data (sms text)
// only, phone number and coding scheme are not parameterized yet. The fixed
// phone number is +1 (650) 123-4567 and the fixed coding scheme is 7 bit
// Alphabet.
class PDUFormatBuilder {
 public:
  void SetUserData(const std::string& user_data);
  // Returns the corresponding PDU format string, returns an empty string if
  // the User Data set is invalid.
  std::string Build();

 private:
  // Encodes using the GSM 7bit encoding as defined in 3GPP TS 23.038
  // https://www.etsi.org/deliver/etsi_ts/123000_123099/123038/09.01.01_60/ts_123038v090101p.pdf
  static std::string Gsm7bitEncode(const std::string& input);
  std::string user_data_;
};

}  // namespace cuttlefish
