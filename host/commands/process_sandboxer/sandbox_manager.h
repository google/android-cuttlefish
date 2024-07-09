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

#include <map>
#include <memory>
#include <string>
#include <vector>

#include <absl/status/statusor.h>
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-parameter"
#include "sandboxed_api/sandbox2/sandbox2.h"
#pragma clang diagnostic pop

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

  absl::Status WaitForExit();

 private:
  SandboxManager() = default;

  HostInfo host_info_;
  std::map<pid_t, std::unique_ptr<sandbox2::Sandbox2>> sandboxes_;
};

}  // namespace process_sandboxer
}  // namespace cuttlefish

#endif
