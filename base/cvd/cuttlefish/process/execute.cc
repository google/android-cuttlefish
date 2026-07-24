/*
 * Copyright (C) 2018 The Android Open Source Project
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

#include "cuttlefish/process/execute.h"

#include <stddef.h>
#include <sys/wait.h>  // IWYU pragma: keep

#include <string>
#include <utility>
#include <vector>

#include "cuttlefish/process/command.h"
#include "cuttlefish/process/subprocess.h"
#include "cuttlefish/result/expect.h"
#include "cuttlefish/result/result_type.h"

namespace cuttlefish {

int Execute(std::vector<std::string> command) {
  // NOLINTNEXTLINE(misc-include-cleaner): <sys/wait.h> provides siginfo_t
  const Result<siginfo_t> result =
      Execute(std::move(command), SubprocessOptions(), WEXITED);
  if (result.has_value() && result->si_code == CLD_EXITED) {
    return result->si_status;  // NOLINT(misc-include-cleaner): <signal.h>
  } else {
    return -1;
  }
}

// NOLINTNEXTLINE(misc-include-cleaner): <sys/wait.h> provides siginfo_t
Result<siginfo_t> Execute(std::vector<std::string> command,
                          SubprocessOptions subprocess_options,
                          int wait_options) {
  Command cmd(std::move(command[0]));
  for (size_t i = 1; i < command.size(); ++i) {
    cmd.AddParameter(std::move(command[i]));
  }
  Subprocess subprocess = cmd.Start(std::move(subprocess_options));
  CF_EXPECT(subprocess.Started(), "Subprocess failed to start.");

  siginfo_t info;
  CF_EXPECT_EQ(subprocess.Wait(&info, wait_options), 0);

  return info;
}

}  // namespace cuttlefish
