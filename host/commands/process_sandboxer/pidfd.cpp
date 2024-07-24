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
#include <sys/syscall.h>
#include <sys/types.h>
#include <unistd.h>

#include <fstream>
#include <memory>
#include <utility>
#include <vector>

#include <absl/status/statusor.h>
#include <absl/strings/numbers.h>
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
  return std::unique_ptr<PidFd>(new PidFd(std::move(fd), pid));
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

}  // namespace process_sandboxer
}  // namespace cuttlefish
