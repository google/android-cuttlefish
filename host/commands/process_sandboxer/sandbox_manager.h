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
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <absl/status/status.h>
#include <absl/status/statusor.h>
#include <absl/types/span.h>
#include <sandboxed_api/sandbox2/policy.h>

#include "host/commands/process_sandboxer/credentialed_unix_server.h"
#include "host/commands/process_sandboxer/policies.h"
#include "host/commands/process_sandboxer/signal_fd.h"
#include "host/commands/process_sandboxer/unique_fd.h"

namespace cuttlefish::process_sandboxer {

class SandboxManager {
 public:
  static absl::StatusOr<std::unique_ptr<SandboxManager>> Create(
      HostInfo host_info);

  SandboxManager(SandboxManager&) = delete;
  ~SandboxManager();

  /** Start a process with the given `argv` and file descriptors in `fds`.
   *
   * For (key, value) pairs in `fds`, `key` on the outside is mapped to `value`
   * in the sandbox, and `key` is `close`d on the outside. */
  absl::Status RunProcess(std::optional<int> client_fd,
                          absl::Span<const std::string> argv,
                          std::vector<std::pair<UniqueFd, int>> fds);

  /** Block until an event happens, and process all open events. */
  absl::Status Iterate();
  bool Running() const;

 private:
  class ManagedProcess {
   public:
    virtual ~ManagedProcess() = default;
    virtual std::optional<int> ClientFd() const = 0;
    virtual int PollFd() const = 0;
    virtual absl::StatusOr<uintptr_t> ExitCode() = 0;
  };
  class ProcessNoSandbox;
  class SandboxedProcess;
  class SocketClient;

  using ClientIter = std::list<std::unique_ptr<SocketClient>>::iterator;
  using SboxIter = std::list<std::unique_ptr<ManagedProcess>>::iterator;

  SandboxManager(HostInfo, std::string runtime_dir, SignalFd,
                 CredentialedUnixServer);

  absl::Status RunSandboxedProcess(std::optional<int> client_fd,
                                   absl::Span<const std::string> argv,
                                   std::vector<std::pair<UniqueFd, int>> fds,
                                   std::unique_ptr<sandbox2::Policy> policy);
  absl::Status RunProcessNoSandbox(std::optional<int> client_fd,
                                   absl::Span<const std::string> argv,
                                   std::vector<std::pair<UniqueFd, int>> fds);

  // Callbacks for the Iterate() `poll` loop.
  absl::Status ClientMessage(ClientIter it, short revents);
  absl::Status NewClient(short revents);
  absl::Status ProcessExit(SboxIter it, short revents);
  absl::Status Signalled(short revents);

  HostInfo host_info_;
  bool running_ = true;
  std::string runtime_dir_;
  std::list<std::unique_ptr<ManagedProcess>> subprocesses_;
  std::list<std::unique_ptr<SocketClient>> clients_;
  SignalFd signals_;
  CredentialedUnixServer server_;
};

}  // namespace cuttlefish::process_sandboxer

#endif
