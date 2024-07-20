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
#ifndef ANDROID_DEVICE_GOOGLE_CUTTLEFISH_HOST_COMMANDS_PROCESS_SANDBOXER_SANDBOX_MANAGER_H
#define ANDROID_DEVICE_GOOGLE_CUTTLEFISH_HOST_COMMANDS_PROCESS_SANDBOXER_SANDBOX_MANAGER_H

#include <list>
#include <memory>
#include <string>
#include <vector>

#include <absl/status/status.h>
#include <absl/status/statusor.h>

#include "host/commands/process_sandboxer/policies.h"

namespace cuttlefish {
namespace process_sandboxer {

class SandboxManager {
 public:
  static absl::StatusOr<std::unique_ptr<SandboxManager>> Create(
      HostInfo host_info);

  SandboxManager(SandboxManager&) = delete;
  ~SandboxManager();

  /** Start a process with the given `argv` and file descriptors in `fds`.
   *
   * For (key, value) pairs in `fds`, `key` on the outside is mapped to `value`
   * in the sandbox, and `key` is `close`d on the outside.
   */
  absl::Status RunProcess(const std::vector<std::string>& argv,
                          const std::map<int, int>& fds);

  /** Block until an event happens, and process all open events. */
  absl::Status Iterate();
  bool Running() const;

 private:
  class ManagedProcess;
  class SocketClient;

  using ClientIter = std::list<std::unique_ptr<SocketClient>>::iterator;
  using SboxIter = std::list<std::unique_ptr<ManagedProcess>>::iterator;

  SandboxManager() = default;

  // Callbacks for the Iterate() `poll` loop.
  absl::Status ClientMessage(ClientIter it, short revents);
  absl::Status NewClient(short revents);
  absl::Status ProcessExit(SboxIter it, short revents);
  absl::Status Signalled(short revents);

  std::string ServerSocketOutsidePath() const;

  HostInfo host_info_;
  bool running_ = true;
  std::string runtime_dir_;
  std::list<std::unique_ptr<ManagedProcess>> sandboxes_;
  std::list<std::unique_ptr<SocketClient>> clients_;
  int signal_fd_ = -1;
  int server_fd_ = -1;
};

}  // namespace process_sandboxer
}  // namespace cuttlefish

#endif
