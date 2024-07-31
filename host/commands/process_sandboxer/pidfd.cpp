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
#include "host/commands/process_sandboxer/pidfd.h"

#include <dirent.h>
#include <signal.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <unistd.h>

#include <fstream>
#include <list>
#include <memory>
#include <utility>
#include <vector>

#include <absl/log/log.h>
#include <absl/status/status.h>
#include <absl/status/statusor.h>
#include <absl/strings/numbers.h>
#include <absl/strings/str_cat.h>
#include <absl/strings/str_format.h>
#include <absl/strings/str_split.h>

#include "host/commands/process_sandboxer/unique_fd.h"

namespace cuttlefish {
namespace process_sandboxer {

absl::StatusOr<std::unique_ptr<PidFd>> PidFd::Create(pid_t pid) {
  UniqueFd fd(syscall(SYS_pidfd_open, pid, 0));  // Always CLOEXEC
  if (fd.Get() < 0) {
    return absl::ErrnoToStatus(errno, "`pidfd_open` failed");
  }
  return std::make_unique<PidFd>(std::move(fd), pid);
}

PidFd::PidFd(UniqueFd fd, pid_t pid) : fd_(std::move(fd)), pid_(pid) {}

int PidFd::Get() const { return fd_.Get(); }

absl::StatusOr<std::vector<std::pair<UniqueFd, int>>> PidFd::AllFds() {
  std::vector<std::pair<UniqueFd, int>> fds;

  std::string dir_name = absl::StrFormat("/proc/%d/fd", pid_);
  std::unique_ptr<DIR, int (*)(DIR*)> dir(opendir(dir_name.c_str()), closedir);
  if (dir.get() == nullptr) {
    return absl::ErrnoToStatus(errno, "`opendir` failed");
  }
  for (dirent* ent = readdir(dir.get()); ent; ent = readdir(dir.get())) {
    int other_fd;
    // `d_name` is guaranteed to be null terminated
    std::string_view name{ent->d_name};
    if (name == "." || name == "..") {
      continue;
    }
    if (!absl::SimpleAtoi(name, &other_fd)) {
      std::string error = absl::StrFormat("'%v/%v' not an int", dir_name, name);
      return absl::InternalError(error);
    }
    // Always CLOEXEC
    UniqueFd our_fd(syscall(SYS_pidfd_getfd, fd_.Get(), other_fd, 0));
    if (our_fd.Get() < 0) {
      return absl::ErrnoToStatus(errno, "`pidfd_getfd` failed");
    }
    fds.emplace_back(std::move(our_fd), other_fd);
  }

  return fds;
}

absl::StatusOr<std::vector<std::string>> PidFd::Argv() {
  auto path = absl::StrFormat("/proc/%d/cmdline", pid_);
  std::ifstream cmdline_file(path, std::ios::binary);
  if (!cmdline_file) {
    auto err = absl::StrFormat("Failed to open '%v'", path);
    return absl::InternalError(err);
  }
  std::stringstream buffer;
  buffer << cmdline_file.rdbuf();
  if (!cmdline_file) {
    auto err = absl::StrFormat("Failed to read '%v'", path);
    return absl::InternalError(err);
  }
  std::vector<std::string> argv = absl::StrSplit(buffer.str(), '\0');
  if (argv.empty()) {
    return absl::InternalError(absl::StrFormat("no argv in '%v'", path));
  } else if (argv.back() == "") {
    argv.pop_back();  // argv ends in an empty string
  }
  return argv;
}

absl::Status PidFd::HaltHierarchy() {
  if (absl::Status stop = SendSignal(SIGSTOP); !stop.ok()) {
    return stop;
  }
  if (absl::Status halt_children = HaltChildHierarchy(); !halt_children.ok()) {
    return halt_children;
  }
  return SendSignal(SIGKILL);
}

/* Assumes the process referred to by `pid` does not spawn any more children or
 * reap any children while this function is running. */
static absl::StatusOr<std::vector<pid_t>> FindChildPids(pid_t pid) {
  std::vector<pid_t> child_pids;

  std::string task_dir = absl::StrFormat("/proc/%d/task", pid);
  std::unique_ptr<DIR, int (*)(DIR*)> dir(opendir(task_dir.c_str()), closedir);
  if (dir.get() == nullptr) {
    return absl::ErrnoToStatus(errno, "`opendir` failed");
  }

  while (dirent* ent = readdir(dir.get())) {
    // `d_name` is guaranteed to be null terminated
    std::string_view name = ent->d_name;
    if (name == "." || name == "..") {
      continue;
    }
    std::string children_file =
        absl::StrFormat("/proc/%d/task/%s/children", pid, name);
    std::ifstream children_stream(children_file);
    if (!children_stream) {
      std::string err = absl::StrCat("can't read child file: ", children_file);
      return absl::InternalError(err);
    }

    std::string children_str;
    std::getline(children_stream, children_str);
    for (std::string_view child_str : absl::StrSplit(children_str, " ")) {
      if (child_str.empty()) {
        continue;
      }
      pid_t child_pid;
      if (!absl::SimpleAtoi(child_str, &child_pid)) {
        std::string error = absl::StrFormat("'%s' is not a pid_t", child_str);
        return absl::InternalError(error);
      }
      child_pids.emplace_back(child_pid);
    }
  }

  return child_pids;
}

absl::Status PidFd::HaltChildHierarchy() {
  absl::StatusOr<std::vector<pid_t>> children = FindChildPids(pid_);
  if (!children.ok()) {
    return children.status();
  }
  for (pid_t child : *children) {
    absl::StatusOr<std::unique_ptr<PidFd>> child_pidfd = Create(child);
    if (!child_pidfd.ok()) {
      return child_pidfd.status();
    }
    // HaltHierarchy will SIGSTOP the child so it cannot spawn more children
    // or reap its own children while everything is being stopped.
    if (absl::Status halt = (*child_pidfd)->HaltHierarchy(); !halt.ok()) {
      return halt;
    }
  }

  return absl::OkStatus();
}

absl::Status PidFd::SendSignal(int signal) {
  if (syscall(SYS_pidfd_send_signal, fd_.Get(), signal, nullptr, 0) < 0) {
    return absl::ErrnoToStatus(errno, "pidfd_send_signal failed");
  }
  return absl::OkStatus();
}

}  // namespace process_sandboxer
}  // namespace cuttlefish
