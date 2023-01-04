/*
 * Copyright (C) 2022 The Android Open Source Project
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
#include "host/commands/cvd_send_sms/pdu_format_builder.h"

#include <algorithm>
#include <codecvt>
#include <cstddef>
#include <iomanip>
#include <iostream>
#include <map>
#include <regex>
#include <sstream>
#include <vector>

#include "android-base/logging.h"
#include "unicode/uchriter.h"
#include "unicode/unistr.h"
#include "unicode/ustring.h"

namespace cuttlefish {

namespace {
// 3GPP TS 23.038 V9.1.1 section 6.2.1 - GSM 7 bit Default Alphabet
// https://www.etsi.org/deliver/etsi_ts/123000_123099/123038/09.01.01_60/ts_123038v090101p.pdf
// clang-format off
const std::vector<std::string> kGSM7BitDefaultAlphabet = {
  "@", "£", "$", "¥", "è", "é", "ù", "ì", "ò", "Ç", "\n", "Ø", "ø", "\r", "Å", "å",
  "Δ", "_", "Φ", "Γ", "Λ", "Ω", "Π", "Ψ", "Σ", "Θ", "Ξ", u8"\uffff" /*ESC*/, "Æ", "æ", "ß", "É",
  " ", "!", "\"", "#", "¤", "%", "&", "'", "(", ")", "*", "+", ",", "-", ".", "/",
  "0", "1", "2", "3", "4", "5", "6", "7", "8", "9", ":", ";", "<", "=", ">", "?",
  "¡", "A", "B", "C", "D", "E", "F", "G", "H", "I", "J", "K", "L", "M", "N", "O",
  "P", "Q", "R", "S", "T", "U", "V", "W", "X", "Y", "Z", "Ä", "Ö", "Ñ", "Ü", "§",
  "¿", "a", "b", "c", "d", "e", "f", "g", "h", "i", "j", "k", "l", "m", "n", "o",
  "p", "q", "r", "s", "t", "u", "v", "w", "x", "y", "z", "ä", "ö", "ñ", "ü", "à",
};
// clang-format on

// Encodes using the GSM 7bit encoding as defined in 3GPP TS 23.038
// https://www.etsi.org/deliver/etsi_ts/123000_123099/123038/09.01.01_60/ts_123038v090101p.pdf
static std::string Gsm7bitEncode(const std::string& input) {
  icu::UnicodeString unicode_str(input.c_str());
  icu::UCharCharacterIterator iter(unicode_str.getTerminatedBuffer(),
                                   unicode_str.length());
  size_t octects_size = unicode_str.length() - (unicode_str.length() / 8);
  std::byte octects[octects_size];
  std::byte* octects_index = octects;
  int bits_to_write_in_prev_octect = 0;
  for (; iter.hasNext(); iter.next()) {
    UChar uchar = iter.current();
    char dest[5];
    UErrorCode uerror_code;
    u_strToUTF8(dest, 5, NULL, &uchar, 1, &uerror_code);
    if (U_FAILURE(uerror_code)) {
      LOG(ERROR) << "u_strToUTF8 failed with error: "
                 << u_errorName(uerror_code) << ", with string: " << input;
      return "";
    }
    std::string character(dest);
    auto found_it = std::find(kGSM7BitDefaultAlphabet.begin(),
                              kGSM7BitDefaultAlphabet.end(), character);
    if (found_it == kGSM7BitDefaultAlphabet.end()) {
      LOG(ERROR) << "Character: " << character
                 << " does not exist in GSM 7 bit Default Alphabet";
      return "";
    }
    std::byte code =
        (std::byte)std::distance(kGSM7BitDefaultAlphabet.begin(), found_it);
    if (iter.hasPrevious()) {
      std::byte prev_octect_value = *(octects_index - 1);
      // Writes the corresponding lowest part in the previous octect.
      *(octects_index - 1) =
          code << (8 - bits_to_write_in_prev_octect) | prev_octect_value;
    }
    if (bits_to_write_in_prev_octect < 7) {
      // Writes the remaining highest part in the current octect.
      *octects_index = code >> bits_to_write_in_prev_octect;
      bits_to_write_in_prev_octect++;
      octects_index++;
    } else {  // bits_to_write_in_prev_octect == 7
      // The 7 bits of the current character were fully packed into the
      // previous octect.
      bits_to_write_in_prev_octect = 0;
    }
  }
  std::stringstream result;
  for (int i = 0; i < octects_size; i++) {
    result << std::setfill('0') << std::setw(2) << std::hex
           << std::to_integer<int>(octects[i]);
  }
  return result.str();
}

// Validates whether the passed phone number conforms to the E.164 specs,
// https://www.itu.int/rec/T-REC-E.164
static bool IsValidE164PhoneNumber(const std::string& number) {
  const static std::regex e164_regex("^\\+?[1-9]\\d{1,14}$");
  return std::regex_match(number, e164_regex);
}

// Encodes numeric values by using the Semi-Octect representation.
static std::string SemiOctectsEncode(const std::string& input) {
  bool length_is_odd = input.length() % 2 == 1;
  int end = length_is_odd ? input.length() - 1 : input.length();
  std::stringstream ss;
  for (int i = 0; i < end; i += 2) {
    ss << input[i + 1];
    ss << input[i];
  }
  if (length_is_odd) {
    ss << "f";
    ss << input[input.length() - 1];
  }
  return ss.str();
}

// Converts to hexadecimal representation filling with a leading 0 if
// necessary.
static std::string DecimalToHexString(int number) {
  std::stringstream ss;
  ss << std::setfill('0') << std::setw(2) << std::hex << number;
  return ss.str();
}
}  // namespace

void PDUFormatBuilder::SetUserData(const std::string& user_data) {
  user_data_ = user_data;
}

void PDUFormatBuilder::SetSenderNumber(const std::string& sender_number) {
  sender_number_ = sender_number;
}

std::string PDUFormatBuilder::Build() {
  if (user_data_.empty()) {
    LOG(ERROR) << "Empty user data.";
    return "";
  }
  if (sender_number_.empty()) {
    LOG(ERROR) << "Empty sender phone number.";
    return "";
  }
  if (!IsValidE164PhoneNumber(sender_number_)) {
    LOG(ERROR) << "Sender phone number"
               << " \"" << sender_number_ << "\" "
               << "does not conform with the E.164 format";
    return "";
  }
  std::string sender_number_without_plus =
      sender_number_[0] == '+' ? sender_number_.substr(1) : sender_number_;
  int ulength = icu::UnicodeString(user_data_.c_str()).length();
  if (ulength > 160) {
    LOG(ERROR) << "Invalid user data as it has more than 160 characters: "
               << user_data_;
    return "";
  }
  std::string encoded = Gsm7bitEncode(user_data_);
  if (encoded.empty()) {
    return "";
  }
  std::stringstream ss;
  ss << "000100" << DecimalToHexString(sender_number_without_plus.length())
     << "91"  // 91 indicates international phone number format.
     << SemiOctectsEncode(sender_number_without_plus)
     << "00"  // TP-PID. Protocol identifier
     << "00"  // TP-DCS. Data coding scheme. The GSM 7bit default alphabet.
     << DecimalToHexString(ulength) << encoded;
  return ss.str();
}
}  // namespace cuttlefish
