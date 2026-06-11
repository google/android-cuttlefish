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

#include "cuttlefish/host/commands/cvd/cli/frontline_parser.h"

#include <string>
#include <vector>

#include "cuttlefish/flag_parser/flag.h"
#include "cuttlefish/host/commands/cvd/cli/selector/selector_common_parser.h"
#include "cuttlefish/host/commands/cvd/cli/types.h"

namespace cuttlefish {

Result<selector::SelectorOptions> ExtractCvdArgs(cvd_common::Args& args) {
  CF_EXPECT(!args.empty());
  // Remove the first argument before parsing selectors
  std::string program = args[0];
  args.erase(args.begin());

  // Parse and remove selector options from the beginning of args. This ensures
  // the selectors are found before the subcommand.
  selector::SelectorOptions selector_options;
  std::vector<Flag> selector_flags =
      selector::BuildCommonSelectorFlags(selector_options);
  CF_EXPECT(ConsumeFlags(selector_flags, args));

  // Re-insert program into arguments
  args.insert(args.begin(), program);

  return selector_options;
}

}  // namespace cuttlefish
