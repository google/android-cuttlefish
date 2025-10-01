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

#include "cuttlefish/host/commands/cvd/cvd.h"

#include <iostream>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include <android-base/file.h>
#include <android-base/logging.h>

#include "cuttlefish/common/libs/utils/environment.h"
#include "cuttlefish/common/libs/utils/flag_parser.h"
#include "cuttlefish/common/libs/utils/result.h"
#include "cuttlefish/host/commands/cvd/cli/command_request.h"
#include "cuttlefish/host/commands/cvd/cli/frontline_parser.h"
#include "cuttlefish/host/commands/cvd/cli/request_context.h"
#include "cuttlefish/host/commands/cvd/instances/instance_manager.h"
#include "cuttlefish/host/commands/cvd/instances/lock/instance_lock.h"

namespace cuttlefish {

Cvd::Cvd(InstanceManager& instance_manager,
         InstanceLockFileManager& lock_file_manager)
    : instance_manager_(instance_manager),
      lock_file_manager_(lock_file_manager) {}

Result<void> Cvd::HandleCommand(
    const std::vector<std::string>& cvd_process_args,
    const std::unordered_map<std::string, std::string>& env,
    const std::vector<std::string>& selector_args) {
  CommandRequest request = CF_EXPECT(CommandRequestBuilder()
                                         .AddArguments(cvd_process_args)
                                         .SetEnv(env)
                                         .AddSelectorArguments(selector_args)
                                         .Build());

  RequestContext context(instance_manager_, lock_file_manager_);
  auto handler = CF_EXPECT(context.Handler(request));
  if (handler->ShouldInterceptHelp()) {
    std::vector<std::string> invocation_args = request.SubcommandArguments();
    if (CF_EXPECT(HasHelpFlag(invocation_args))) {
      std::cout << CF_EXPECT(handler->DetailedHelp(invocation_args))
                << std::endl;
      return {};
    }
  }
  CF_EXPECT(handler->Handle(request));
  return {};
}

Result<void> Cvd::HandleCvdCommand(
    const std::vector<std::string>& all_args,
    const std::unordered_map<std::string, std::string>& env) {
  CF_EXPECT(!all_args.empty());
  std::vector<std::string> args = all_args;
  if (args.size() == 1ul) {
    args = cvd_common::Args{"cvd", "help"};
  }
  std::vector<std::string> selector_args = CF_EXPECT(ExtractCvdArgs(args));
  // TODO(schuffelen): Deduplicate cvd process split.
  CF_EXPECT(HandleCommand(args, env, selector_args));
  return {};
}

}  // namespace cuttlefish
