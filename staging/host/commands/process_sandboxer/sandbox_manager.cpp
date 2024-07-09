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
#include "host/commands/process_sandboxer/sandbox_manager.h"

#include <unistd.h>

#include <absl/log/log.h>
#include <absl/status/status.h>
#include <absl/status/statusor.h>
#include <absl/strings/str_format.h>
#include "sandboxed_api/util/path.h"

#include "host/commands/process_sandboxer/policies.h"

using absl::OkStatus;
using absl::Status;
using absl::StatusCode;
using absl::StatusOr;
using sandbox2::Executor;
using sandbox2::Sandbox2;
using sapi::file::CleanPath;

namespace cuttlefish {
namespace process_sandboxer {

StatusOr<std::unique_ptr<SandboxManager>> SandboxManager::Create(
    HostInfo host_info) {
  std::unique_ptr<SandboxManager> manager(new SandboxManager());
  manager->host_info_ = std::move(host_info);
  return manager;
}

SandboxManager::~SandboxManager() {
  for (const auto& [pid, sandbox] : sandboxes_) {
    sandbox->Kill();
    auto res = sandbox->AwaitResult().ToStatus();
    if (!res.ok()) {
      LOG(ERROR) << "Issue in closing sandbox: '" << res.ToString() << "'";
    }
  }
}

Status SandboxManager::RunProcess(const std::vector<std::string>& argv,
                                  const std::map<int, int>& fds) {
  if (argv.empty()) {
    for (const auto& [fd_inner, fd_outer] : fds) {
      if (close(fd_outer) < 0) {
        LOG(ERROR) << "Failed to close '" << fd_inner << "'";
      }
    }
    return Status(StatusCode::kInvalidArgument, "Not enough arguments");
  }
  auto exe = CleanPath(argv[0]);
  auto executor = std::make_unique<Executor>(exe, argv);

  // https://cs.android.com/android/platform/superproject/main/+/main:external/sandboxed-api/sandboxed_api/sandbox2/limits.h;l=116;drc=d451478e26c0352ecd6912461e867a1ae64b17f5
  // Default is 120 seconds
  executor->limits()->set_walltime_limit(absl::InfiniteDuration());
  // Default is 1024 seconds
  executor->limits()->set_rlimit_cpu(RLIM64_INFINITY);

  for (const auto& [fd_outer, fd_inner] : fds) {
    executor->ipc()->MapFd(fd_outer,
                           fd_inner);  // Will close `fd_outer` in this process
  }

  std::unique_ptr<Sandbox2> sandbox(
      new Sandbox2(std::move(executor), PolicyForExecutable(host_info_, exe)));
  if (!sandbox->RunAsync()) {
    return sandbox->AwaitResult().ToStatus();
  }

  auto pid = sandbox->pid();
  sandboxes_.emplace(pid, std::move(sandbox));

  return OkStatus();
}

Status SandboxManager::WaitForExit() {
  std::vector<Status> results;
  for (const auto& [pid, sandbox] : sandboxes_) {
    results.emplace_back(sandbox->AwaitResult().ToStatus());
  }
  for (const auto& result : results) {
    if (!result.ok()) {
      return result;
    }
  }
  return OkStatus();
}

}  // namespace process_sandboxer
}  // namespace cuttlefish
