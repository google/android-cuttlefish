/*
 * Copyright (C) 2023 The Android Open Source Project
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

#include <sys/types.h>

#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "common/libs/utils/result.h"
#include "host/commands/cvd/types.h"

namespace cuttlefish {
namespace selector {

class SelectorCommonParser {
 public:
  // parses common selector options, and drop the used selector_args
  static Result<SelectorCommonParser> Parse(const uid_t client_uid,
                                            cvd_common::Args& selector_args,
                                            const cvd_common::Envs& envs);

  std::optional<std::string> GroupName() const { return group_name_; }

  std::optional<std::vector<std::string>> PerInstanceNames() const {
    return instance_names_;
  }

  // CF_ERR --> unknown, true --> overridden, false --> not overridden.
  Result<bool> HomeOverridden() const;

  /*
   * returns if selector flags has device select options: e.g. --group_name
   *
   * this is mainly to see if cvd start is about the default instance.
   */
  bool HasDeviceSelectOption() const { return group_name_ || instance_names_; }

 private:
  SelectorCommonParser(const std::string& client_user_home,
                       cvd_common::Args& selector_args,
                       const cvd_common::Envs& envs);

  Result<void> ParseOptions();
  struct ParsedNameFlags {
    std::optional<std::string> group_name;
    std::optional<std::vector<std::string>> instance_names;
  };
  struct NameFlagsParam {
    std::optional<std::string> group_name;
    std::optional<std::string> instance_names;
  };
  Result<ParsedNameFlags> HandleNameOpts(
      const NameFlagsParam& name_flags) const;
  Result<std::string> HandleGroupName(
      const std::optional<std::string>& group_name) const;
  Result<std::vector<std::string>> HandleInstanceNames(
      const std::optional<std::string>& per_instance_names) const;

  // temporarily keeps the leftover of the input cmd_args
  // Will be never used after parsing is done
  // Never be nullptr as it is addressof(object).
  std::string client_user_home_;
  // these are pointers as the SelectorCommonParser is movable, and
  // selector_args_ and envs_ must not be moved along with other fields
  cvd_common::Args* selector_args_;
  const cvd_common::Envs* envs_;

  // processed result
  std::optional<std::string> group_name_;
  std::optional<std::vector<std::string>> instance_names_;
};

}  // namespace selector
}  // namespace cuttlefish
