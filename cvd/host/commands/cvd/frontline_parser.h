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

#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>

#include "common/libs/utils/json.h"
#include "common/libs/utils/result.h"
#include "host/commands/cvd/client.h"
#include "host/commands/cvd/selector/arguments_separator.h"
#include "host/commands/cvd/types.h"

namespace cuttlefish {

/* the very first command line parser
 *
 * Being aware of valid subcommands and cvd-specific commands, it will
 * separate the command line arguments into:
 *
 *  1. program path/name
 *  2. cvd-specific arguments
 *     a) selector flags
 *     b) non-selector flags
 *  3. subcommand
 *  4. subcommand arguments
 *
 * This is currently on the client side but will be moved to the server
 * side.
 */
class FrontlineParser {
  using ArgumentsSeparator = selector::ArgumentsSeparator;

 public:
  // This call must guarantee all public methods will be valid
  static Result<std::unique_ptr<FrontlineParser>> Parse(
      CvdClient& client, const std::vector<std::string>& internal_cmds,
      const cvd_common::Args& all_args, const cvd_common::Envs& envs);

  const std::string& ProgPath() const;
  std::optional<std::string> SubCmd() const;
  const cvd_common::Args& SubCmdArgs() const;
  const cvd_common::Args& SelectorArgs() const;
  bool Clean() const { return clean_; }
  bool Help() const { return help_; }

 private:
  FrontlineParser(CvdClient& client,
                  const std::vector<std::string>& internal_cmds,
                  const cvd_common::Args& all_args,
                  const cvd_common::Envs& envs);

  // internal workers in order
  Result<void> Separate();
  Result<cvd_common::Args> ValidSubcmdsList();
  Result<std::unique_ptr<ArgumentsSeparator>> CallSeparator();
  struct FilterOutput {
    bool clean;
    bool help;
    cvd_common::Args selector_args;
  };
  Result<FilterOutput> FilterNonSelectorArgs();

  /*
   * Returns the list of subcommands that cvd ever supports.
   *
   * The tool is for now intended to be internal to the parser that uses
   * command line arguments separator.
   *
   */
  Result<Json::Value> ListSubcommands();

  CvdClient& client_;
  std::unordered_set<std::string> known_bool_flags_;
  std::unordered_set<std::string> known_value_flags_;
  std::unordered_set<std::string> selector_flags_;
  cvd_common::Args valid_subcmds_;
  const cvd_common::Args all_args_;
  const cvd_common::Envs envs_;
  const std::vector<std::string>& internal_cmds_;
  std::unique_ptr<ArgumentsSeparator> arguments_separator_;

  // outputs
  bool clean_;
  bool help_;
  /**
   * remaining arguments to pass to the selector
   */
  cvd_common::Args selector_args_;
};

}  // namespace cuttlefish
