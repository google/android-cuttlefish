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

using selector::SeparateArguments;
using selector::SeparatedArguments;

Result<cvd_common::Args> ExtractCvdArgs(cvd_common::Args& args) {
  CF_EXPECT(!args.empty());

  SeparatedArguments separated_arguments = CF_EXPECT(SeparateArguments(args));

  cvd_common::Args new_exec_args{separated_arguments.prog_path};

  const std::optional<std::string>& sub_cmd = separated_arguments.sub_cmd;
  if (sub_cmd) {
    new_exec_args.push_back(*sub_cmd);
  }

  const cvd_common::Args& sub_cmd_args{separated_arguments.sub_cmd_args};
  new_exec_args.insert(new_exec_args.end(), sub_cmd_args.begin(),
                       sub_cmd_args.end());

  args = new_exec_args;
  return separated_arguments.cvd_args;
}

}  // namespace cuttlefish
