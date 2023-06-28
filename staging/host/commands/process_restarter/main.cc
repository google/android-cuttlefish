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

#include <sys/wait.h>

#include <cstdlib>
#include <optional>
#include <string>
#include <vector>

#include <android-base/strings.h>

#include "common/libs/utils/result.h"
#include "common/libs/utils/subprocess.h"
#include "host/commands/process_restarter/parser.h"
#include "host/libs/config/logging.h"

namespace cuttlefish {
namespace {

static bool ShouldRestartProcess(siginfo_t const& info, const Parser& parsed) {
  if (info.si_code == CLD_DUMPED && parsed.WhenDumped()) {
    return true;
  }
  if (info.si_code == CLD_KILLED && parsed.WhenKilled()) {
    return true;
  }
  if (info.si_code == CLD_EXITED && parsed.WhenExitedWithFailure() &&
      info.si_status != 0) {
    return true;
  }
  if (info.si_code == CLD_EXITED &&
      info.si_status == parsed.WhenExitedWithCode()) {
    return true;
  }
  return false;
}

Result<int> RunProcessRestarter(std::vector<std::string> args) {
  LOG(VERBOSE) << "process_restarter starting";
  auto parsed = CF_EXPECT(Parser::ConsumeAndParse(args));

  // move-assign the remaining args to exec_args
  const std::vector<std::string> exec_args = std::move(args);
  const std::string& exec_cmd = exec_args.front();

  siginfo_t info;
  do {
    LOG(VERBOSE) << "Starting monitored process " << exec_cmd;
    // The Execute() API and all APIs effectively called by it show the proper
    // error message using LOG(ERROR).
    info = CF_EXPECT(
        Execute(exec_args, SubprocessOptions().ExitWithParent(true), WEXITED),
        "Executing " << android::base::Join(exec_args, " ") << " failed.");
  } while (ShouldRestartProcess(info, parsed));
  return info.si_status;
}

}  // namespace
}  // namespace cuttlefish

int main(int argc, char** argv) {
  cuttlefish::DefaultSubprocessLogging(argv);
  auto result = cuttlefish::RunProcessRestarter(
      std::move(cuttlefish::ArgsToVec(argc - 1, argv + 1)));
  if (!result.ok()) {
    LOG(ERROR) << result.error().Message();
    LOG(DEBUG) << result.error().Trace();
    return EXIT_FAILURE;
  }
  return result.value();
}
