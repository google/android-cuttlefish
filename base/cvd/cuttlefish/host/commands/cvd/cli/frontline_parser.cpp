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

#include "host/commands/cvd/cli/frontline_parser.h"

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <android-base/file.h>
#include <android-base/strings.h>

#include "host/commands/cvd/cli/flag.h"
#include "host/commands/cvd/cli/selector/arguments_separator.h"
#include "host/commands/cvd/cli/selector/selector_constants.h"
#include "host/commands/cvd/cli/types.h"

namespace cuttlefish {

using selector::ArgumentsSeparator;

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
 */
Result<cvd_common::Args> ExtractCvdArgs(cvd_common::Args& args) {
  CF_EXPECT(!args.empty());

  ArgumentsSeparator::FlagsRegistration flag_registration{
      .known_boolean_flags = {},
      .known_value_flags = {selector::SelectorFlags::kGroupName,
                            selector::SelectorFlags::kInstanceName,
                            selector::SelectorFlags::kVerbosity},
  };
  auto arguments_separator =
      CF_EXPECT(ArgumentsSeparator::Parse(flag_registration, args));
  CF_EXPECT(arguments_separator != nullptr);

  cvd_common::Args new_exec_args{arguments_separator->ProgPath()};

  const auto new_sub_cmd = arguments_separator->SubCmd();
  if (new_sub_cmd) {
    new_exec_args.push_back(*new_sub_cmd);
  }

  cvd_common::Args cmd_args{arguments_separator->SubCmdArgs()};
  new_exec_args.insert(new_exec_args.end(), cmd_args.begin(), cmd_args.end());

  args = new_exec_args;
  return arguments_separator->CvdArgs();
}

}  // namespace cuttlefish
