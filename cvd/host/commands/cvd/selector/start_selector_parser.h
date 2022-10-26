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

#include <optional>
#include <string>
#include <unordered_set>
#include <vector>

#include "common/libs/utils/flag_parser.h"
#include "common/libs/utils/result.h"

namespace cuttlefish {
namespace selector {

struct SeparatedArguments {
  std::vector<std::string> before_selector_opts;
  std::vector<std::string> selector_specific;
  std::vector<std::string> after_selector_opts;
};

/**
 * takes cmdline arguments, and separate them into 3 pieces above
 */
Result<SeparatedArguments> SeparateArguments(
    const std::vector<std::string>& args);

/**
 * This class parses the separated SelectorOptions defined in
 * cvd_server.proto.
 *
 * Note that the parsing is from the perspective of syntax.
 *
 * In other words, this does not check the following, for example:
 *  1. If the numeric instance id is duplicated
 *  2. If the group name is already taken
 *
 */
class SelectorFlagsParser {
 public:
  static Result<SelectorFlagsParser> ConductSelectFlagsParser(
      const std::vector<std::string>& args);
  std::optional<std::string> GroupName() const;
  std::optional<std::vector<std::string>> PerInstanceNames() const;
  const auto& SubstringQueries() const { return substring_queries_; }

 private:
  SelectorFlagsParser(const std::vector<std::string>& args);

  /*
   * Note: name may or may not be valid. A name could be a
   * group name or a device name or an instance name, depending
   * on the context: i.e. the operation.
   *
   * This succeeds only if all selector arguments can be legitimately
   * consumed.
   */
  Result<void> ParseOptions();

  bool IsValidName(const std::string& name) const;
  Result<std::unordered_set<std::string>> FindSubstringsToMatch();
  struct ParsedNameFlags {
    std::optional<std::string> group_name;
    std::optional<std::vector<std::string>> instance_names;
  };
  struct NameFlagsParam {
    std::optional<std::string> names;
    std::optional<std::string> device_names;
    std::optional<std::string> group_name;
    std::optional<std::string> instance_names;
  };
  Result<ParsedNameFlags> HandleNameOpts(
      const NameFlagsParam& name_flags) const;
  /*
   * As --name could give a device list, a group list, or a per-
   * instance list, HandleNames() will set some of them according
   * to the syntax.
   */
  Result<ParsedNameFlags> HandleNames(
      const std::optional<std::string>& names) const;
  struct DeviceNamesPair {
    std::string group_name;
    std::vector<std::string> instance_names;
  };
  Result<DeviceNamesPair> HandleDeviceNames(
      const std::optional<std::string>& device_names) const;
  Result<std::vector<std::string>> HandleInstanceNames(
      const std::optional<std::string>& per_instance_names) const;
  Result<std::string> HandleGroupName(
      const std::optional<std::string>& group_name) const;

  std::optional<std::string> group_name_;
  std::optional<std::vector<std::string>> instance_names_;
  std::unordered_set<std::string> substring_queries_;

  // temporarily keeps the leftover of the input args
  std::vector<std::string> args_;
};

}  // namespace selector
}  // namespace cuttlefish
