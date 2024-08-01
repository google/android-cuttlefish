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
#include <absl/types/span.h>

#include "host/commands/process_sandboxer/unique_fd.h"

namespace cuttlefish {
namespace process_sandboxer {

class PidFd {
 public:
  /** Returns a managed pidfd tracking a previously started process with `pid`.
   *
   * Only reliably refers to the process `pid` if the caller can guarantee it
   * was not reaped while this is executing, otherwise it may refer to an
   * unknown process. */
  static absl::StatusOr<PidFd> FromRunningProcess(pid_t pid);

  /** Launches a subprocess and returns a pidfd tracking the newly launched
   * process. */
  static absl::StatusOr<PidFd> LaunchSubprocess(
      absl::Span<const std::string> argv,
      std::vector<std::pair<UniqueFd, int>> fds);

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
  PidFd(UniqueFd, pid_t);
  absl::Status SendSignal(int signal);

  UniqueFd fd_;
  pid_t pid_;
};

}  // namespace process_sandboxer
}  // namespace cuttlefish
#endif
