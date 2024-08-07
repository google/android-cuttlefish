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
#include "host/commands/process_sandboxer/signal_fd.h"

#include <signal.h>
#include <sys/signalfd.h>

#include <absl/status/status.h>
#include <absl/status/statusor.h>
#include <absl/strings/str_cat.h>

#include "host/commands/process_sandboxer/unique_fd.h"

namespace cuttlefish::process_sandboxer {

SignalFd::SignalFd(UniqueFd fd) : fd_(std::move(fd)) {}

absl::StatusOr<SignalFd> SignalFd::AllExceptSigChld() {
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

  UniqueFd fd(signalfd(-1, &mask, SFD_CLOEXEC | SFD_NONBLOCK));
  if (fd.Get() < 0) {
    return absl::ErrnoToStatus(errno, "signalfd failed");
  }
  return SignalFd(std::move(fd));
}

absl::StatusOr<signalfd_siginfo> SignalFd::ReadSignal() {
  signalfd_siginfo info;
  auto read_res = read(fd_.Get(), &info, sizeof(info));
  if (read_res < 0) {
    return absl::ErrnoToStatus(errno, "`read(signal_fd_, ...)` failed");
  } else if (read_res == 0) {
    return absl::InternalError("read(signal_fd_, ...) returned EOF");
  } else if (read_res != (ssize_t)sizeof(info)) {
    std::string err = absl::StrCat("read(signal_fd_, ...) gave '", read_res);
    return absl::InternalError(err);
  }
  return info;
}

int SignalFd::Fd() const { return fd_.Get(); }

}  // namespace cuttlefish::process_sandboxer
