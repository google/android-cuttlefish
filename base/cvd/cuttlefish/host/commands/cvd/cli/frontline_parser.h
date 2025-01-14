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

#include "common/libs/utils/result.h"
#include "host/commands/cvd/cli/selector/arguments_separator.h"
#include "host/commands/cvd/cli/types.h"

namespace cuttlefish {

Result<cvd_common::Args> ExtractCvdArgs(cvd_common::Args& args);

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
      const cvd_common::Args& all_args);

  const std::string& ProgPath() const;
  std::optional<std::string> SubCmd() const;
  const cvd_common::Args& SubCmdArgs() const;
  const cvd_common::Args& CvdArgs() const;

 private:
  FrontlineParser(const cvd_common::Args& all_args);

  // internal workers in order
  Result<void> Separate();
  Result<std::unique_ptr<ArgumentsSeparator>> CallSeparator();
  struct FilterOutput {
    bool clean;
    bool help;
    cvd_common::Args selector_args;
  };
  Result<FilterOutput> FilterNonSelectorArgs();

  const cvd_common::Args all_args_;
  const std::vector<std::string> internal_cmds_;
  std::unique_ptr<ArgumentsSeparator> arguments_separator_;
};

}  // namespace cuttlefish
