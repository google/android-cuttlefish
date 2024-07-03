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

#include <signal.h>
#include <sys/eventfd.h>
#include <sys/signalfd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <memory>
#include <sstream>
#include <utility>

#include <absl/functional/bind_front.h>
#include <absl/log/log.h>
#include <absl/log/vlog_is_on.h>
#include <absl/status/status.h>
#include <absl/status/statusor.h>
#include <absl/strings/str_cat.h>
#include <absl/strings/str_format.h>
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-parameter"
#include <sandboxed_api/sandbox2/executor.h>
#include <sandboxed_api/sandbox2/policy.h>
#include <sandboxed_api/sandbox2/sandbox2.h>
#include <sandboxed_api/util/path.h>
#pragma clang diagnostic pop

#include "host/commands/process_sandboxer/policies.h"
#include "host/commands/process_sandboxer/poll_callback.h"

namespace cuttlefish::process_sandboxer {

using sandbox2::Executor;
using sandbox2::Policy;
using sandbox2::Sandbox2;
using sapi::file::CleanPath;
using sapi::file::JoinPath;

class SandboxManager::ManagedProcess {
 public:
  ManagedProcess(UniqueFd event_fd, std::unique_ptr<Sandbox2> sandbox)
      : event_fd_(std::move(event_fd)), sandbox_(std::move(sandbox)) {
    waiter_thread_ = std::thread([this]() {
      sandbox_->AwaitResult().IgnoreResult();
      uint64_t buf = 1;
      if (write(event_fd_.Get(), &buf, sizeof(buf)) < 0) {
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
  }

  int EventFd() const { return event_fd_.Get(); }

 private:
  UniqueFd event_fd_;
  std::thread waiter_thread_;
  std::unique_ptr<Sandbox2> sandbox_;
};

class SandboxManager::SocketClient {
 public:
  SocketClient(UniqueFd client_fd) : client_fd_(std::move(client_fd)) {}
  SocketClient(SocketClient&) = delete;

  int ClientFd() const { return client_fd_.Get(); }

  absl::Status HandleMessage() {
    char buf;
    if (read(client_fd_.Get(), &buf, sizeof(buf)) < 0) {
      return absl::ErrnoToStatus(errno, "`read` failed");
    }
    return absl::UnimplementedError("TODO(schuffelen)");
  }

 private:
  UniqueFd client_fd_;
};

absl::StatusOr<std::unique_ptr<SandboxManager>> SandboxManager::Create(
    HostInfo host_info) {
  std::unique_ptr<SandboxManager> manager(new SandboxManager());
  manager->host_info_ = std::move(host_info);
  manager->runtime_dir_ =
      absl::StrFormat("/tmp/sandbox_manager.%u.XXXXXX", getpid());
  if (mkdtemp(manager->runtime_dir_.data()) == nullptr) {
    return absl::ErrnoToStatus(errno, "mkdtemp failed");
  }
  VLOG(1) << "Created temporary directory '" << manager->runtime_dir_ << "'";

  sigset_t mask;
  if (sigfillset(&mask) < 0) {
    return absl::ErrnoToStatus(errno, "sigfillset failed");
  }
  // TODO(schuffelen): Explore interaction between catching SIGCHLD and sandbox2
  if (sigdelset(&mask, SIGCHLD) < 0) {
    return absl::ErrnoToStatus(errno, "sigdelset failed");
  }
  if (sigprocmask(SIG_SETMASK, &mask, NULL) < 0) {
    return absl::ErrnoToStatus(errno, "sigprocmask failed");
  }
  VLOG(1) << "Blocked signals";

  manager->signal_fd_.Reset(signalfd(-1, &mask, SFD_CLOEXEC | SFD_NONBLOCK));
  if (manager->signal_fd_.Get() < 0) {
    return absl::ErrnoToStatus(errno, "signalfd failed");
  }
  VLOG(1) << "Created signalfd";

  manager->server_fd_.Reset(socket(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0));
  if (manager->server_fd_.Get() < 0) {
    return absl::ErrnoToStatus(errno, "`socket` failed");
  }
  sockaddr_un socket_name = {
      .sun_family = AF_UNIX,
  };
  std::snprintf(socket_name.sun_path, sizeof(socket_name.sun_path), "%s",
                manager->ServerSocketOutsidePath().c_str());
  auto sockname_ptr = reinterpret_cast<sockaddr*>(&socket_name);
  if (bind(manager->server_fd_.Get(), sockname_ptr, sizeof(socket_name)) < 0) {
    return absl::ErrnoToStatus(errno, "`bind` failed");
  }
  if (listen(manager->server_fd_.Get(), 10) < 0) {
    return absl::ErrnoToStatus(errno, "`listen` failed");
  }

  return manager;
}

SandboxManager::~SandboxManager() {
  VLOG(1) << "Sandbox shutting down";
  if (!runtime_dir_.empty()) {
    if (unlink(ServerSocketOutsidePath().c_str()) < 0) {
      PLOG(ERROR) << "`unlink` failed";
    }
    if (rmdir(runtime_dir_.c_str()) < 0) {
      PLOG(ERROR) << "Failed to remove '" << runtime_dir_ << "'";
    }
  }
}

absl::Status SandboxManager::RunProcess(
    const std::vector<std::string>& argv,
    std::vector<std::pair<UniqueFd, int>> fds) {
  if (argv.empty()) {
    return absl::InvalidArgumentError("Not enough arguments");
  }

  if (VLOG_IS_ON(1)) {
    std::stringstream process_stream;
    process_stream << "Launching executable with argv: [\n";
    for (const auto& arg : argv) {
      process_stream << "\t\"" << arg << "\",\n";
    }
    process_stream << "] with FD mapping: [\n";
    for (const auto& [fd_in, fd_out] : fds) {
      process_stream << '\t' << fd_in.Get() << " -> " << fd_out << ",\n";
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

  for (auto& [fd_outer, fd_inner] : fds) {
    // Will close `fd_outer` in this process
    executor->ipc()->MapFd(fd_outer.Release(), fd_inner);
  }

  UniqueFd event_fd(eventfd(0, EFD_CLOEXEC));
  if (event_fd.Get() < 0) {
    return absl::ErrnoToStatus(errno, "`eventfd` failed");
  }

  std::unique_ptr<Sandbox2> sandbox(
      new Sandbox2(std::move(executor), PolicyForExecutable(host_info_, exe)));
  if (!sandbox->RunAsync()) {
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

  sandboxes_.emplace_back(
      new ManagedProcess(std::move(event_fd), std::move(sandbox)));

  return absl::OkStatus();
}

bool SandboxManager::Running() const { return running_; }

absl::Status SandboxManager::Iterate() {
  PollCallback poll_cb;

  poll_cb.Add(signal_fd_.Get(), bind_front(&SandboxManager::Signalled, this));
  poll_cb.Add(server_fd_.Get(), bind_front(&SandboxManager::NewClient, this));

  for (auto it = sandboxes_.begin(); it != sandboxes_.end(); it++) {
    int fd = (*it)->EventFd();
    poll_cb.Add(fd, bind_front(&SandboxManager::ProcessExit, this, it));
  }
  for (auto it = clients_.begin(); it != clients_.end(); it++) {
    int fd = (*it)->ClientFd();
    poll_cb.Add(fd, bind_front(&SandboxManager::ClientMessage, this, it));
  }

  return poll_cb.Poll();
}

absl::Status SandboxManager::Signalled(short revents) {
  if (revents != POLLIN) {
    running_ = false;
    return absl::InternalError("signalfd exited");
  }
  signalfd_siginfo info;
  auto read_res = read(signal_fd_.Get(), &info, sizeof(info));
  if (read_res < 0) {
    return absl::ErrnoToStatus(errno, "`read(signal_fd_, ...)` failed");
  } else if (read_res == 0) {
    return absl::InternalError("read(signal_fd_, ...) returned EOF");
  } else if (read_res != (ssize_t)sizeof(info)) {
    std::string err = absl::StrCat("read(signal_fd_, ...) gave '", read_res);
    return absl::InternalError(err);
  }
  VLOG(1) << "Received signal with signo '" << info.ssi_signo << "'";

  switch (info.ssi_signo) {
    case SIGHUP:
    case SIGINT:
    case SIGTERM:
      LOG(INFO) << "Received signal '" << info.ssi_signo << "', exiting";
      running_ = false;
      return absl::OkStatus();
    default:
      std::string err = absl::StrCat("Unexpected signal ", info.ssi_signo);
      return absl::InternalError(err);
  }
}

absl::Status SandboxManager::NewClient(short revents) {
  if (revents != POLLIN) {
    running_ = false;
    return absl::InternalError("server socket exited");
  }
  UniqueFd client(accept4(server_fd_.Get(), nullptr, nullptr, SOCK_CLOEXEC));
  if (client.Get() < 0) {
    return absl::ErrnoToStatus(errno, "`accept` failed");
  }
  clients_.emplace_back(new SocketClient(std::move(client)));
  return absl::OkStatus();
}

absl::Status SandboxManager::ProcessExit(SandboxManager::SboxIter it,
                                         short revents) {
  sandboxes_.erase(it);
  static constexpr char kErr[] = "eventfd exited";
  return revents == POLLIN ? absl::OkStatus() : absl::InternalError(kErr);
}

absl::Status SandboxManager::ClientMessage(SandboxManager::ClientIter it,
                                           short rev) {
  if (rev == POLLIN) {
    return (*it)->HandleMessage();
  }
  clients_.erase(it);
  return absl::InternalError("client dropped file descriptor");
}

std::string SandboxManager::ServerSocketOutsidePath() const {
  return JoinPath(runtime_dir_, "/", "server.sock");
}

}  // namespace cuttlefish::process_sandboxer
