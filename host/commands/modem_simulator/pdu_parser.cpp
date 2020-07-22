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

#include "host/commands/modem_simulator/pdu_parser.h"

#include <unistd.h>

#include <algorithm>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <string>

namespace cuttlefish {

static const std::string kWithoutServiceCenterAddress     = "00";
static const std::string kStatusReportIndicator           = "06";
static const std::string kSRIAndMMSIndicator              = "24";  /* SRI is 1 && MMS is 1*/
static const std::string kUDHIAndSRIAndMMSIndicator       = "64";  /* UDHI is 1 && SRI is 1 && MMS is 1*/

PDUParser::PDUParser(std::string &pdu) {
  is_valid_pdu_ = DecodePDU(pdu);
}

bool PDUParser::IsValidPDU() {
  return is_valid_pdu_;
}

/**
 * PDU format:
 *         SCA    PDU-Type   MR         OA            PID    DCS   VP    UDL    UD
 * bytes: 1-12      1        1         2-12            1      1     0     1    0-140
 * eg.     00       21       00   0B 91 5155255155F4   00     00          0C  AB58AD56ABC962B55A8D06
 */
//    00 01 00 05 81 0180F6 00 00 0D 61B2996C0691CD6433190402
bool PDUParser::DecodePDU(std::string& pdu) {
  // At least: SCA(1) + PDU-Type(1) + MR(1) + OA(2) + PID(1) + DSC(1) + UDL(1)
  auto pdu_total_length = pdu.size();
  if (pdu_total_length < 8) {
    return false;
  }

  std::string_view pdu_view = pdu;
  size_t pos = 0;

  /* 1. SMSC Address Length: 1 byte */
  std::string temp = pdu.substr(0, 2);
  pos += 2;
  if (temp != kWithoutServiceCenterAddress) {
    auto smsc_length = Hex2ToByte(temp);
    pos += smsc_length * 2;  // Skip SMSC Address
  }

  /* 2. PDU-Type: 1 byte */
  pdu_type_ = pdu_view.substr(std::min(pos, pdu_total_length), 2);
  pos += 2;

  /* 3. MR: 1 byte */
  message_reference_ = pdu_view.substr(std::min(pos, pdu_total_length), 2);
  pos += 2;

  /* 4. Originator Address Length: 1 byte */
  temp = pdu_view.substr(std::min(pos, pdu_total_length), 2);
  auto oa_length = Hex2ToByte(temp);
  if (oa_length & 0x01) oa_length += 1;

  /* 5. Originator Address including OA length */
  originator_address_ = pdu_view.substr(std::min(pos, pdu_total_length), (oa_length + 4));
  pos += (oa_length + 4);

  /* 6. Protocol ID: 1 byte */
  protocol_id_ = pdu_view.substr(std::min(pos, pdu_total_length), 2);
  pos += 2;

  /* 7. Data Code Scheme: 1 byte */
  data_code_scheme_ = pdu_view.substr(std::min(pos, pdu_total_length), 2);
  pos += 2;

  /* 8. User Data Length: 1 byte */
  temp = pdu_view.substr(std::min(pos, pdu_total_length), 2);
  auto ud_length = Hex2ToByte(temp);

  /* 9. User Data including UDL */
  user_data_ = pdu_view.substr(std::min(pos, pdu_total_length));

  if (data_code_scheme_ == "00") {  // GSM_7BIT
    pos += ud_length * 2 + 2;
    int offset = ud_length / 8;
    pos -= offset * 2;
  } else if (data_code_scheme_ == "08") {  // GSM_UCS2
    pos += ud_length;
  } else {
    pos += ud_length * 2 + 2;
  }
  if (pos == pdu_total_length) {
    return true;
  }

  return false;
}

/**
 * The PDU-Type of receiver
 * BIT      7    6    5    4    3    2    1    0
 * Param   RP  UDHI  SRI  －    －   MMS  MTI MTI
 * When SRR bit is 1, it represents that SMS status report should be reported.
 */
std::string PDUParser::CreatePDU() {
  if (!is_valid_pdu_) return "";

  // Ignore SMSC address, default to be '00'
  std::string pdu = kWithoutServiceCenterAddress;
  int pdu_type = Hex2ToByte(pdu_type_);

  if (pdu_type & 0x40) {
    pdu += kUDHIAndSRIAndMMSIndicator;
  } else {
    pdu += kSRIAndMMSIndicator;
  }

  pdu += originator_address_ + protocol_id_ + data_code_scheme_;
  pdu += GetCurrentTimeStamp();
  pdu += user_data_;

  return pdu;
}

/**
 * the PDU-Type of sender
 * BIT     7    6    5    4    3    2    1    0
 * Param   RP  UDHI  SRR  VPF  VPF  RD    MTI MTI
 * When SRR bit is 1, it represents that SMS status report should be reported.
 */
bool PDUParser::IsNeededStatuReport() {
  if (!is_valid_pdu_) return false;

  int pdu_type = Hex2ToByte(pdu_type_);
  if (pdu_type & 0x20) {
    return true;
  }

  return false;
}

std::string PDUParser::CreateStatuReport(int message_reference) {
  if (!is_valid_pdu_) return "";

  std::string pdu = kWithoutServiceCenterAddress;
  pdu += kStatusReportIndicator;

  std::stringstream ss;
  ss << std::setfill('0') << std::setw(2) << std::hex << message_reference;
  pdu += ss.str();

  pdu += originator_address_;
  pdu += GetCurrentTimeStamp();
  sleep(1);
  pdu += GetCurrentTimeStamp();
  pdu += "00"; /* "00" means that SMS have been sent successfully */

  return pdu;
}

std::string PDUParser::CreateRemotePDU(std::string& host_port) {
  if (host_port.size() != 4 || !is_valid_pdu_) {
    return "";
  }

  std::string pdu = kWithoutServiceCenterAddress + pdu_type_ + message_reference_;

  // Remove the remote port
  std::string number = GetPhoneNumberFromAddress();
  auto new_phone_number = number.substr(0, number.size() - 4);;
  new_phone_number.append(host_port);
  if (new_phone_number.size() & 1) {
    new_phone_number.append("F");
  }

  // Add OA length and type
  pdu += originator_address_.substr(0,
      originator_address_.size() - new_phone_number .size());
  pdu += BCDToString(new_phone_number);   // Add local host port
  pdu += protocol_id_;
  pdu += data_code_scheme_;
  pdu += user_data_;

  return pdu;
}

std::string PDUParser::GetPhoneNumberFromAddress() {
  if (!is_valid_pdu_) return "";

  // Skip OA length and type
  std::string address;
  if (originator_address_.size() == 18) {
    address = originator_address_.substr(6);
  } else {
    address = originator_address_.substr(4);
  }

  return BCDToString(address);
}

int PDUParser::HexCharToInt(char c) {
  if (c >= '0' && c <= '9') return (c - '0');
  if (c >= 'A' && c <= 'F') return (c - 'A' + 10);
  if (c >= 'a' && c <= 'f') return (c - 'a' + 10);

  return -1;  // Invalid hex char
}

int PDUParser::Hex2ToByte(const std::string& hex) {
  int  hi = HexCharToInt(hex[0]);
  int  lo = HexCharToInt(hex[1]);

  if (hi < 0 || lo < 0) {
    return -1;
  }

  return ( (hi << 4) | lo );
}

std::string PDUParser::IntToHexString(int value) {
  int  hi = value / 10;
  int  lo = value % 10;
  return std::to_string(lo) + std::to_string(hi);
}

std::string PDUParser::BCDToString(std::string& data) {
  std::string dst;
  if (data.empty()) {
    return "";
  }
  int length = data.size();
  if (length & 0x01) {  /* Must be even */
    return "";
  }
  for (int i = 0; i < length; i += 2) {
    dst += data[i + 1];
    dst += data[i];
  }

  if (dst[length -1] == 'F') {
    dst.replace(length -1, length, "\0");
  }
  return dst;
}

std::string PDUParser::GetCurrentTimeStamp() {
  std::string time_stamp;
  auto now = std::time(0);

  auto local_time = *std::localtime(&now);
  auto gm_time = *std::gmtime(&now);

  auto t_local_time = std::mktime(&local_time);
  auto t_gm_time = std::mktime(&gm_time);

  auto tzdiff = (int)std::difftime(t_local_time, t_gm_time) / (60 * 60);

  time_stamp += IntToHexString(local_time.tm_year % 100);
  time_stamp += IntToHexString(local_time.tm_mon + 1);
  time_stamp += IntToHexString(local_time.tm_mday);
  time_stamp += IntToHexString(local_time.tm_hour);
  time_stamp += IntToHexString(local_time.tm_min);
  time_stamp += IntToHexString(local_time.tm_sec);
  time_stamp += IntToHexString(tzdiff);

  return time_stamp;
}

} // namespace cuttlefish
