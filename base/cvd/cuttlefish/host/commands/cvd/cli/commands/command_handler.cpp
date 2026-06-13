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

#include "cuttlefish/host/commands/cvd/cli/commands/command_handler.h"

#include <stddef.h>

#include <algorithm>
#include <sstream>
#include <string>
#include <vector>

#include "cuttlefish/flag_parser/flag.h"
#include "cuttlefish/host/commands/cvd/cli/command_request.h"
#include "cuttlefish/host/commands/cvd/cli/help_format.h"
#include "cuttlefish/host/commands/cvd/cli/selector/selector_common_parser.h"
#include "cuttlefish/host/commands/cvd/cli/types.h"
#include "cuttlefish/result/result.h"

namespace cuttlefish {

bool CvdCommandHandler::RequiresDeviceExists() const { return false; }

std::vector<HelpParagraph> CvdCommandHandler::Description() const { return {}; }

Result<std::vector<Flag>> CvdCommandHandler::Flags(const CommandRequest&) {
  return {};
}

Result<std::string> CvdCommandHandler::DetailedHelp(
    const CommandRequest& request) {
  std::stringstream ss;
  std::vector<std::string> cmd_list = CmdList();
  CF_EXPECT(!cmd_list.empty(), "Command aliases list is empty");

  ss << "cvd " << cmd_list[0] << " - " << SummaryHelp() << "\n";
  ss << "\n";
  ss << FormatHelpText(Description());

  std::vector<Flag> flags = CF_EXPECT(Flags(request));
  // Consume flags to ensure "current value" is accurate
  cvd_common::Args args = request.SubcommandArguments();
  CF_EXPECT(ConsumeFlags(flags, args));

  // Make sure the flags are in alphabetical order
  std::sort(flags.begin(), flags.end());

  selector::SelectorOptions selector_options = request.Selectors();
  if (RequiresDeviceExists()) {
    // Add the common selector flags if the command supports them. This doesn't
    // need to hapen before consuming as the selector flags were consumed
    // already. Using the selectors from the request ensures the flags's
    // "current value" is correct.
    std::vector<Flag> selector_flags =
        selector::BuildCommonSelectorFlags(selector_options);
    flags.insert(flags.begin(), selector_flags.begin(), selector_flags.end());
  }

  ss << FormatFlagsHelp(flags);

  return ss.str();
}

}  // namespace cuttlefish
