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

#include <android-base/strings.h>

namespace cuttlefish {

class CommandParser {
 public:
  explicit CommandParser(const std::string& command) : copy_command_(command) {
    command_ = copy_command_;
  };

  ~CommandParser() = default;

  CommandParser(CommandParser&&) = default;
  CommandParser& operator=(CommandParser&&) = default;

  inline void SkipPrefix();
  inline void SkipPrefixAT();
  inline void SkipComma();
  inline void SkipWhiteSpace();

  std::string_view GetNextStr();
  std::string_view GetNextStr(char flag);
  std::string GetNextStrDeciToHex();  /* for AT+CRSM */

  int GetNextInt();
  int GetNextHexInt();

  const std::string_view* operator->() const { return &command_; }
  const std::string_view& operator*() const { return command_; }
  bool operator==(const std::string &rhs) const { return command_ == rhs; }
  std::string_view::const_reference& operator[](int index) const { return command_[index]; }

 private:
  std::string copy_command_;
  std::string_view command_;
};

/**
 * Skip the substring before the first '=', including '='
 * updates command_
 * If '=' not exists, command_ remains unchanged
 */
inline void CommandParser::SkipPrefix() {
  auto pos = command_.find('=');
  if (pos != std::string_view::npos) {
    command_.remove_prefix(std::min(pos + 1, command_.size()));
  }
}

/**
 * Skip the next "AT" substring
 * updates command_
 */
inline void CommandParser::SkipPrefixAT() {
  android::base::ConsumePrefix(&command_, std::string_view("AT"));
}

/**
 * Skip the next comma
 * updates command_
 */
inline void CommandParser::SkipComma() {
  auto pos = command_.find(',');
  if (pos != std::string_view::npos) {
    command_.remove_prefix(std::min(pos + 1, command_.size()));
  }
}

/**
 * Skip the next whitespace
 * updates command_
 */
inline void CommandParser::SkipWhiteSpace() {
  auto pos = command_.find(' ');
  if (pos != std::string_view::npos) {
    command_.remove_prefix(std::min(pos + 1, command_.size()));
  }
}

}  // namespace cuttlefish
