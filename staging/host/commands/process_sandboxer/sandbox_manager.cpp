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

#include <poll.h>
#include <signal.h>
#include <sys/eventfd.h>
#include <sys/signalfd.h>
#include <unistd.h>

#include <memory>
#include <sstream>

#include <absl/log/log.h>
#include <absl/log/vlog_is_on.h>
#include <absl/status/status.h>
#include <absl/status/statusor.h>
#include <absl/strings/str_format.h>
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-parameter"
#include <sandboxed_api/sandbox2/executor.h>
#include <sandboxed_api/sandbox2/policy.h>
#include <sandboxed_api/sandbox2/sandbox2.h>
#include <sandboxed_api/util/path.h>
#pragma clang diagnostic pop

#include "host/commands/process_sandboxer/policies.h"

using absl::ErrnoToStatusCode;
using absl::OkStatus;
using absl::Status;
using absl::StatusCode;
using absl::StatusOr;
using sandbox2::Executor;
using sandbox2::Policy;
using sandbox2::Sandbox2;
using sapi::file::CleanPath;

namespace cuttlefish {
namespace process_sandboxer {

class SandboxManager::ManagedProcess {
 public:
  ManagedProcess(int event_fd, std::unique_ptr<Sandbox2> sandbox)
      : event_fd_(event_fd), sandbox_(std::move(sandbox)) {
    waiter_thread_ = std::thread([this]() {
      sandbox_->AwaitResult().IgnoreResult();
      uint64_t buf = 1;
      if (write(event_fd_, &buf, sizeof(buf)) < 0) {
        PLOG(ERROR) << "Failed to write to eventfd";
      }
    });
  }
  ManagedProcess(ManagedProcess&) = delete;
  ~ManagedProcess() {
    sandbox_->Kill();
    waiter_thread_.join();
    auto res = sandbox_->AwaitResult().ToStatus();
    if (!res.ok()) {
      LOG(ERROR) << "Issue in closing sandbox: '" << res.ToString() << "'";
    }
    if (close(event_fd_) < 0) {
      PLOG(ERROR) << "`close(event_fd_)` failed";
    }
  }

  int EventFd() const { return event_fd_; }

 private:
  int event_fd_;
  std::thread waiter_thread_;
  std::unique_ptr<Sandbox2> sandbox_;
};

StatusOr<std::unique_ptr<SandboxManager>> SandboxManager::Create(
    HostInfo host_info) {
  std::unique_ptr<SandboxManager> manager(new SandboxManager());
  manager->host_info_ = std::move(host_info);
  manager->runtime_dir_ =
      absl::StrFormat("/tmp/sandbox_manager.%u.XXXXXX", getpid());
  if (mkdtemp(manager->runtime_dir_.data()) == nullptr) {
    return Status(ErrnoToStatusCode(errno), "mkdtemp failed");
  }
  VLOG(1) << "Created temporary directory '" << manager->runtime_dir_ << "'";

  sigset_t mask;
  if (sigfillset(&mask) < 0) {
    return Status(ErrnoToStatusCode(errno), "sigfillset failed");
  }
  // TODO(schuffelen): Explore interaction between catching SIGCHLD and sandbox2
  if (sigdelset(&mask, SIGCHLD) < 0) {
    return Status(ErrnoToStatusCode(errno), "sigdelset failed");
  }
  if (sigprocmask(SIG_SETMASK, &mask, NULL) < 0) {
    return Status(ErrnoToStatusCode(errno), "sigprocmask failed");
  }
  VLOG(1) << "Blocked signals";

  manager->signal_fd_ = signalfd(-1, &mask, SFD_CLOEXEC | SFD_NONBLOCK);
  if (manager->signal_fd_ < 0) {
    return Status(ErrnoToStatusCode(errno), "signalfd failed");
  }
  VLOG(1) << "Created signalfd";
  return manager;
}

SandboxManager::~SandboxManager() {
  VLOG(1) << "Sandbox shutting down";
  if (!runtime_dir_.empty() && rmdir(runtime_dir_.c_str()) < 0) {
    LOG(ERROR) << "Failed to remove '" << runtime_dir_ << "'";
  }
  if (signal_fd_ >= 0 && close(signal_fd_) < 0) {
    LOG(ERROR) << "Failed to close signal fd '" << signal_fd_ << "'";
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

  int event_fd = eventfd(0, EFD_CLOEXEC);
  if (event_fd < 0) {
    return Status(ErrnoToStatusCode(errno), "`eventfd` failed");
  }

  if (VLOG_IS_ON(1)) {
    std::stringstream process_stream;
    process_stream << "Launching executable with argv: [\n";
    for (const auto& arg : argv) {
      process_stream << "\t\"" << arg << "\",\n";
    }
    process_stream << "] with FD mapping: [\n";
    for (const auto& [fd_in, fd_out] : fds) {
      process_stream << '\t' << fd_in << " -> " << fd_out << ",\n";
    }
    process_stream << "]\n";
    VLOG(1) << process_stream.str();
  }

  auto exe = CleanPath(argv[0]);
  auto executor = std::make_unique<Executor>(exe, argv);
  executor->set_cwd(host_info_.runtime_dir);

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
    if (close(event_fd) < 0) {
      PLOG(ERROR) << "`close(event_fd)` failed";
    }
    return sandbox->AwaitResult().ToStatus();
  }

  // A pidfd over the sandbox is another option, but there are two problems:
  //
  // 1. There's a race between launching the sandbox and opening the pidfd. If
  // the sandboxed process exits too quickly, the monitor thread in sandbox2
  // could reap it and another process could reuse the pid before `pidfd_open`
  // runs. Sandbox2 could produce a pidfd itself using `CLONE_PIDFD`, but it
  // does not do this at the time of writing.
  //
  // 2. The sandbox could outlive its top-level process. It's not clear to me if
  // sandbox2 allows this in practice, but `AwaitResult` could theoretically
  // wait on subprocesses of the original sandboxed process as well.
  //
  // To deal with these concerns, we use another thread blocked on AwaitResult
  // that signals the eventfd when sandbox2 says the sandboxed process has
  // exited.

  sandboxes_.emplace_back(new ManagedProcess(event_fd, std::move(sandbox)));

  return OkStatus();
}

bool SandboxManager::Running() const { return running_; }

Status SandboxManager::Iterate() {
  std::vector<pollfd> poll_fds;
  poll_fds.emplace_back(pollfd{
      .fd = signal_fd_,
      .events = POLLIN,
  });
  for (const auto& managed : sandboxes_) {
    poll_fds.emplace_back(pollfd{
        .fd = managed->EventFd(),
        .events = POLLIN,
    });
  }

  if (poll(poll_fds.data(), poll_fds.size(), /* timeout = */ 0) < 0) {
    return Status(ErrnoToStatusCode(errno), "`poll` failed");
  }

  for (const auto& poll_fd : poll_fds) {
    if (poll_fd.revents == 0) {
      continue;
    }
    if (poll_fd.fd == signal_fd_) {
      auto status = HandleSignal();
      if (!status.ok()) {
        return status;
      }
    }
    for (auto it = sandboxes_.begin(); it != sandboxes_.end();) {
      it = poll_fd.fd == (*it)->EventFd() ? sandboxes_.erase(it) : ++it;
    }
  }

  return OkStatus();
}

Status SandboxManager::HandleSignal() {
  signalfd_siginfo info;
  auto read_res = read(signal_fd_, &info, sizeof(info));
  if (read_res < 0) {
    return Status(ErrnoToStatusCode(errno), "`read(signal_fd_, ...)` failed");
  } else if (read_res == 0) {
    return Status(StatusCode::kInternal, "read(signal_fd_, ...) returned EOF");
  } else if (read_res != (ssize_t)sizeof(info)) {
    return Status(StatusCode::kInternal, "read(signal_fd_, ...) gave bad size");
  }
  VLOG(1) << "Received signal with signo '" << info.ssi_signo << "'";

  switch (info.ssi_signo) {
    case SIGHUP:
    case SIGINT:
    case SIGTERM:
      LOG(INFO) << "Received signal '" << info.ssi_signo << "', exiting";
      running_ = false;
      return OkStatus();
    default:
      return Status(StatusCode::kInternal, "Received unexpected signal");
  }
}

}  // namespace process_sandboxer
}  // namespace cuttlefish
