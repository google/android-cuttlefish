#include "host/commands/cvd_send_sms/pdu_format_builder.h"

#include <algorithm>
#include <codecvt>
#include <cstddef>
#include <iomanip>
#include <iostream>
#include <map>
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
}  // namespace

void PDUFormatBuilder::SetUserData(const std::string& user_data) {
  user_data_ = user_data;
}

std::string PDUFormatBuilder::Build() {
  if (user_data_.empty()) {
    LOG(ERROR) << "Empty user data.";
    return "";
  }
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
  ss << "0001000B916105214365F70000";
  ss << std::setfill('0') << std::setw(2) << std::hex << ulength << encoded;
  return ss.str();
}

std::string PDUFormatBuilder::Gsm7bitEncode(const std::string& input) {
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

}  // namespace cuttlefish
