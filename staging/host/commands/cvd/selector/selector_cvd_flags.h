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

#pragma once

#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "common/libs/utils/flag_parser.h"
#include "common/libs/utils/result.h"

namespace cuttlefish {
namespace selector {

// TODO(kwstephenkim): add more selector options

/**
 * This class parses the separated SelectorOptions defined in
 * cvd_server.proto.
 *
 * Note that the parsing is from the perspective of syntax
 *
 */
class SelectorFlagsParser {
 public:
  static Result<SelectorFlagsParser> ConductSelectFlagsParser(
      const std::vector<std::string>& args);

  bool HasName() const;
  std::string Name() const;
  const auto& SubstringQueries() const { return substring_queries_; }

 private:
  SelectorFlagsParser(const std::vector<std::string>& args);

  /*
   * Note: name may or may not be valid. A name could be a
   * group name or a device name or an instance name, depending
   * on the context: i.e. the operation.
   *
   * Here, we only check if the name is a qualified token in
   * terms of lexing rules.
   *
   * This succeeds only if all args_ can be legitimately consumed.
   */
  Result<void> Parse();

  bool IsNameValid(const std::string& name) const;
  /*
   * All positional arguments left in selector arguments would be
   * regarded as substring match keywords.
   */
  Result<std::unordered_set<std::string>> FindSubstringsToMatch();

  // temporarily keeps the leftover of the input args
  std::vector<std::string> args_;
  std::unordered_map<std::string, std::string> opt_value_map_;
  std::unordered_set<std::string> substring_queries_;
};

}  // namespace selector
}  // namespace cuttlefish
