/*
 * Copyright (C) 2024 The Android Open Source Project
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
#ifndef ANDROID_DEVICE_GOOGLE_CUTTLEFISH_HOST_COMMANDS_SANDBOX_PROCESS_PIDFD_H
#define ANDROID_DEVICE_GOOGLE_CUTTLEFISH_HOST_COMMANDS_SANDBOX_PROCESS_PIDFD_H

#include <sys/types.h>

#include <utility>
#include <vector>

#include <absl/status/statusor.h>

#include "host/commands/process_sandboxer/unique_fd.h"

namespace cuttlefish {
namespace process_sandboxer {

class PidFd {
 public:
  PidFd(UniqueFd, pid_t);
  static absl::StatusOr<std::unique_ptr<PidFd>> Create(pid_t pid);

  int Get() const;

  /** Copies file descriptors from the target process, mapping them into the
   * current process.
   *
   * Keys are file descriptor numbers in the target process, values are open
   * file descriptors in the current process.
   */
  absl::StatusOr<std::vector<std::pair<UniqueFd, int>>> AllFds();
  absl::StatusOr<std::vector<std::string>> Argv();

  /** Halt the process and all its descendants. */
  absl::Status HaltHierarchy();
  /** Halt all descendants of the process. Only safe to use if the caller
   * guarantees the process doesn't spawn or reap any children while running. */
  absl::Status HaltChildHierarchy();

 private:
  absl::Status SendSignal(int signal);

  UniqueFd fd_;
  pid_t pid_;
};

}  // namespace process_sandboxer
}  // namespace cuttlefish
#endif
