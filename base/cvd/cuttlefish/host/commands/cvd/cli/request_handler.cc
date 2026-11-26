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

#include "cuttlefish/host/commands/cvd/cli/request_handler.h"

#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "fmt/base.h"
#include "fmt/ostream.h"
#include "fmt/ranges.h"

#include "cuttlefish/common/libs/utils/contains.h"
#include "cuttlefish/host/commands/cvd/cli/command_request.h"
#include "cuttlefish/host/commands/cvd/cli/commands/command_handler.h"
#include "cuttlefish/host/commands/cvd/cli/handler_list.h"
#include "cuttlefish/result/result.h"

namespace cuttlefish {

namespace {

bool CanHandle(const CvdCommandHandler& handler,
               const CommandRequest& request) {
  return Contains(handler.CmdList(), request.Subcommand());
}

std::vector<std::string> GetPossibleCommands(
    const CommandRequest& request,
    const std::vector<std::unique_ptr<CvdCommandHandler>>& handlers) {
  std::vector<std::string> possibilities;
  if (request.Subcommand().empty()) {
    return possibilities;
  }

  for (auto& handler : handlers) {
    for (const std::string& command : handler->CmdList()) {
      if (!command.empty() && command.front() == request.Subcommand().front()) {
        possibilities.push_back(command);
        break;
      }
    }
  }
  return possibilities;
}

bool HandleDeprecatedCommands(const CommandRequest& request) {
  if (request.Subcommand() == "acloud") {
    std::cerr << "If you are seeing this error when you tried to run `acloud` "
                 "in an Android lunch environment, you are likely running into "
                 "an error with an outdated `acloud_translator` symlink.  You "
                 "can verify "
                 "with `where acloud`, and there should be two results.\n\nTo "
                 "fix, run `rm $ANDROID_HOST_OUT/bin/acloud` to remove the "
                 "deprecated `acloud_translator` binary symlink.  `m clean; m` "
                 "should also remove the symlink.\n";
    return true;
  }
  return false;
}

void SuggestAlternativeCommands(
    const CommandRequest& request,
    const std::vector<std::unique_ptr<CvdCommandHandler>>& handlers) {
  fmt::print(std::cerr,
             "Unable to find a matching command for \"cvd {}\".\nMaybe there "
             "is a typo?  Run `cvd help` for a list of commands.",
             request.Subcommand());
  const std::vector<std::string> possible_commands =
      GetPossibleCommands(request, handlers);
  if (!possible_commands.empty()) {
    fmt::print(std::cerr, "\n\nDid you mean one of:\n\t{}\n",
               fmt::join(possible_commands, "\n\t"));
  }
}

void HandleNoMatches(
    const CommandRequest& request,
    const std::vector<std::unique_ptr<CvdCommandHandler>>& handlers) {
  if (!HandleDeprecatedCommands(request)) {
    SuggestAlternativeCommands(request, handlers);
  }
}

}  //  namespace

Result<CvdCommandHandler*> RequestHandler(const CommandRequest& request,
                                          const HandlerList& handlers) {
  std::vector<CvdCommandHandler*> compatible_handlers;
  for (auto& handler : handlers.Handlers()) {
    if (CanHandle(*handler, request)) {
      compatible_handlers.push_back(handler.get());
    }
  }

  CF_EXPECT_LE(compatible_handlers.size(), 1,
               "The command matched multiple handlers, which should not "
               "happen.  Please open a bug with the Cuttlefish team and "
               "include the exact command that raised the error.");

  if (compatible_handlers.size() != 1) {
    HandleNoMatches(request, handlers.Handlers());
    return CF_ERRF("Unable to find matching command for \"cvd {}\".",
                   request.Subcommand());
  }
  return compatible_handlers[0];
}

}  // namespace cuttlefish
