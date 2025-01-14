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
#include <unordered_set>
#include <vector>

#include "cuttlefish/host/commands/cvd/legacy/cvd_server.pb.h"

#include "common/libs/utils/result.h"
#include "host/commands/cvd/cli/selector/arguments_lexer.h"

namespace cuttlefish {
namespace selector {

struct SeparatedArguments {
  std::string prog_path;
  std::vector<std::string> cvd_args;
  std::optional<std::string> sub_cmd;
  std::vector<std::string> sub_cmd_args;
};

/**
 * The very first parser for cmdline that separates:
 *
 *  1. program name/path
 *  2. cvd specific options such as --clean, selector options, etc
 *  3. subcmd
 *  4. subcmd arguments
 *
 * Note that the user's command line arguments are in this order:
 *  $ program_path/name <optional cvd-specific flags> \
 *                      subcmd <optional subcmd arguments>
 *
 * For the parser's sake, there are a few more rules.
 *
 * 1. All the optional cvd-specific flags should be pre-registered. Usually,
 * the subcmd arguments do not have to be registered. However, cvd-specific
 * flags must be.
 *
 *  E.g. "--clean" is the only registered cvd-specific flag, which happened
 *      to be bool.
 *       These are okay:
 *         cvd --clean start --never-exist-flag
 *         cvd --noclean stop
 *         cvd start
 *
 *       However, this is not okay:
 *        cvd --daemon start
 *
 *  2. --
 *  E.g. cvd --clean start --have --some --args -- a b c d e
 *  -- is basically for subcommands. cvd itself does not use it.
 *  If -- is within cvd arguments, it is ill-formatted. If it is within
 *  subcommands arguments, we simply forward it to the subtool as is.
 *
 */
Result<SeparatedArguments> SeparateArguments(
    const std::vector<std::string>& input_args);

}  // namespace selector
}  // namespace cuttlefish
