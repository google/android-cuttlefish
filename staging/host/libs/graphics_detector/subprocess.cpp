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

#include "host/libs/graphics_detector/subprocess.h"

#include <dlfcn.h>
#include <sys/wait.h>

#include <android-base/logging.h>

namespace cuttlefish {

SubprocessResult DoWithSubprocessCheck(std::string check_name,
                                       std::function<void()> function) {
  LOG(INFO) << "Running " << check_name << " in subprocess...";
  pid_t pid = fork();
  if (pid == 0) {
    function();
    std::exit(0);
  }

  LOG(INFO) << "Waiting for subprocess running " << check_name << "...";

  int status;
  if (waitpid(pid, &status, 0) != pid) {
    LOG(INFO) << "Failed to wait for subprocess running " << check_name << ".";
    return SubprocessResult::kFailure;
  }
  if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
    LOG(INFO) << "Subprocess running " << check_name
              << " succeeded. Running in this process...";
    function();
    return SubprocessResult::kFailure;
  } else {
    LOG(INFO) << "Subprocess running " << check_name
              << " failed. Not running in this process.";
    return SubprocessResult::kFailure;
  }
}

}  // namespace cuttlefish