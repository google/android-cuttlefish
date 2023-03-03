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

#include "common/libs/utils/result.h"
#include "host/commands/cvd/client.h"
#include "host/commands/cvd/flag.h"
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
  struct ParserParam {
    // commands supported by the server
    std::vector<std::string> server_supported_subcmds;
    // commands supported by the client itself
    std::vector<std::string> internal_cmds;
    cvd_common::Args all_args;
    FlagCollection cvd_flags;
  };

  // This call must guarantee all public methods will be valid
  static Result<std::unique_ptr<FrontlineParser>> Parse(ParserParam param);

  const std::string& ProgPath() const;
  std::optional<std::string> SubCmd() const;
  const cvd_common::Args& SubCmdArgs() const;
  const cvd_common::Args& CvdArgs() const;

 private:
  FrontlineParser(const ParserParam& parser);

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

  cvd_common::Args server_supported_subcmds_;
  const cvd_common::Args all_args_;
  const std::vector<std::string> internal_cmds_;
  FlagCollection cvd_flags_;
  std::unique_ptr<ArgumentsSeparator> arguments_separator_;
};

}  // namespace cuttlefish
