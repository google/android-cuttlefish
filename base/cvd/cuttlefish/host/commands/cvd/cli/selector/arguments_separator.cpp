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

#include "host/commands/cvd/cli/selector/arguments_separator.h"

#include <android-base/strings.h>

#include "host/commands/cvd/cli/selector/selector_common_parser.h"

namespace cuttlefish {
namespace selector {

/*
 * prog_name, <optional cvd flags>, sub_cmd, <optional sub_cmd flags>
 *
 * -- could be included, which makes things complicated. However, if -- is
 * part of cvd flags, it's ill-formatted. If -- is among sub_cmd flags,
 * we will just forward it.
 *
 * If something like this is really needed, use the suggested alternative:
 *    original: cvd --some_flag -- --this-is-value start --subcmd_args
 * alternative: cvd --some_flag="--this-is-value" start --subcmd_args
 *
 */
Result<SeparatedArguments> SeparateArguments(
    const std::vector<std::string>& input_args) {
  CF_EXPECT(!input_args.empty());

  SeparatedArguments output;

  std::vector<std::string> args = input_args;

  output.prog_path = args[0];
  args.erase(args.begin());

  // Mutates `args` to remove selector arguments
  SelectorOptions selectors = CF_EXPECT(ParseCommonSelectorArguments(args));
  output.cvd_args = selectors.AsArgs();

  if (!args.empty()) {
    output.sub_cmd = args[0];
    args.erase(args.begin());
  }

  output.sub_cmd_args = args;

  return output;
}

}  // namespace selector
}  // namespace cuttlefish
