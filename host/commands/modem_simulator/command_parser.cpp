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

#include <string>
#include <sstream>

#include "command_parser.h"

namespace cuttlefish {

/**
 * Parses the next string between double quotes
 * returns the string on success and "" on fail
 * updates command_
 */
std::string_view CommandParser::GetNextStr() {
  auto fpos = command_.find('\"');
  if (fpos == std::string_view::npos) {
    return {};
  }

  auto spos = command_.find('\"', fpos + 1);
  if (spos == std::string_view::npos) {
    command_ = command_.substr(fpos + 1);
    return {};
  }

  auto str = command_.substr(fpos + 1, (spos - fpos - 1));
  command_ = command_.substr(spos + 1);
  SkipComma();
  return str;
}

/**
 * Parses the next string before the flag
 * If flag not exists, returns the whole command_
 * updates command_
 */
std::string_view CommandParser::GetNextStr(char flag) {
  auto pos = command_.find(flag);
  auto str = command_.substr(0, pos);
  if (pos != std::string_view::npos) pos += 1;  // npos + 1 = 0
  command_.remove_prefix(std::min(pos, command_.size()));
  return str;
}

/**
 * Parses the next base 10 integer in the AT command and convert to upper case
 * hex string
 * returns the hex string on success and "" on fail
 * updates command_
 *
 * Specially, for AT+CRSM
 */
std::string CommandParser::GetNextStrDeciToHex() {
  std::string str;
  int value = GetNextInt();
  if (value == -1) {
    return {};
  } else {
    std::stringstream ss;
    ss << std::hex << std::uppercase << value;
    return ss.str();
  }
}

static int parse_int(const std::string& snumber, int base) {
  if (snumber.empty()) return -1;
  const char* p = snumber.c_str();
  char* p_end = nullptr;
  errno = 0;
  const long lval = std::strtol(p, &p_end, base);
  if (p == p_end) {
    return -1;
  }
  const bool range_error = errno == ERANGE;

  if (range_error) return -1;
  return lval;
}

/**
 * Parses the next base 10 integer in the AT command
 * returns the value on success and -1 on fail
 * updates command_
 */
int CommandParser::GetNextInt() {
  if (command_.empty()) {
    return -1;
  }
  std::string sub(GetNextStr(','));

  int value = parse_int(sub, 10);

  return value;
}

/**
 * Parses the next base 16 integer in the AT command
 * returns the value on success and -1 on fail
 * updates command_
 */
int CommandParser::GetNextHexInt() {
  if (command_.empty()) {
    return -1;
  }

  std::string sub(GetNextStr(','));
  int value = parse_int(sub, 16);
  return value;
}

}  // namespace cuttlefish
