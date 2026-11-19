/*
 * Copyright (C) 2023 The Android Open Source Project
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

#include "cuttlefish/host/commands/cvd/instances/lock/lock_file.h"

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/file.h>
#include <sys/types.h>
#include <unistd.h>

#include <algorithm>
#include <cstring>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "absl/log/log.h"
#include "absl/strings/numbers.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/strings/strip.h"

#include "cuttlefish/common/libs/fs/fd.h"
#include "cuttlefish/common/libs/fs/shared_fd.h"
#include "cuttlefish/common/libs/utils/environment.h"
#include "cuttlefish/common/libs/utils/files.h"
#include "cuttlefish/common/libs/utils/in_sandbox.h"
#include "cuttlefish/files/directory_exists.h"
#include "cuttlefish/result/result.h"

namespace cuttlefish {
namespace {

bool DefaultCreateLocksOnDemand() {
#ifdef __linux__
  return InSandbox();
#else
  return true;
#endif
}

Result<void> SetStatus(SharedFD& fd, const InUseState state) {
  CF_EXPECT(fd->Truncate(0));
  CF_EXPECT(fd->LSeek(0, SEEK_SET) == 0, fd->StrError());
  std::string payload = state == InUseState::kInUse
                            ? absl::StrCat("I ", geteuid(), "\n")
                            : std::string("N\n");
  CF_EXPECT(fd->Write(payload.data(), payload.size()) ==
                static_cast<uint64_t>(payload.size()),
            fd->StrError());
  return {};
}

}  // namespace

LockFile::LockFileReleaser::LockFileReleaser(const SharedFD& fd,
                                             const std::string& lock_file_path)
    : flocked_file_fd_(fd), lock_file_path_(lock_file_path) {}

LockFile::LockFileReleaser::~LockFileReleaser() {
  if (!flocked_file_fd_->IsOpen()) {
    LOG(ERROR) << "SharedFD to " << lock_file_path_
               << " is closed and unable to un-flock()";
    return;
  }
  Result<void, int> funlock_result = flocked_file_fd_->Flock(LOCK_UN | LOCK_NB);
  if (!funlock_result.has_value()) {
    LOG(ERROR) << "Unlock the \"" << lock_file_path_
               << "\" failed: " << funlock_result.error();
  }
}

LockFile::LockFile(SharedFD fd, const std::string& lock_file_path)
    : fd_(std::move(fd)),
      lock_file_path_(lock_file_path),
      lock_file_lock_releaser_{
          std::make_shared<LockFile::LockFileReleaser>(fd_, lock_file_path_)} {}

Result<InUseState> LockFile::Status() const {
  CF_EXPECT(fd_->LSeek(0, SEEK_SET) == 0, fd_->StrError());
  char state_char = static_cast<char>(InUseState::kNotInUse);
  CF_EXPECT(fd_->Read(&state_char, 1).has_value(), fd_->StrError());
  switch (state_char) {
    case static_cast<char>(InUseState::kInUse):
      return InUseState::kInUse;
    case static_cast<char>(InUseState::kNotInUse):
      return InUseState::kNotInUse;
    default:
      return CF_ERRF("Unexpected state value \"{}\"", state_char);
  }
}

Result<std::optional<uid_t>> LockFile::HolderUid() const {
  CF_EXPECT(fd_->LSeek(0, SEEK_SET) == 0, fd_->StrError());
  char buf[64] = {};
  size_t bytes_read = CF_EXPECT(fd_->Read(buf, sizeof(buf) - 1));
  absl::string_view content(buf, bytes_read);
  (void)absl::ConsumeSuffix(&content, "\n");
  if (!absl::ConsumePrefix(&content, "I ")) {
    return std::nullopt;
  }
  unsigned parsed_uid = 0;
  if (!absl::SimpleAtoi(content, &parsed_uid)) {
    return std::nullopt;
  }
  return static_cast<uid_t>(parsed_uid);
}

Result<void> LockFile::Status(InUseState state) {
  CF_EXPECT(SetStatus(fd_, state));
  return {};
}

bool LockFile::operator<(const LockFile& other) const {
  if (this == std::addressof(other)) {
    return false;
  }
  if (LockFilePath() == other.LockFilePath()) {
    return fd_ < other.fd_;
  }
  // operator< for std::string will be gone as of C++20
  return (strncmp(lock_file_path_.data(), other.LockFilePath().data(),
                  std::max(lock_file_path_.size(),
                           other.LockFilePath().size())) < 0);
}

LockFileManager::LockFileManager()
    : create_missing_locks_(DefaultCreateLocksOnDemand()) {}

LockFileManager::LockFileManager(bool create_missing_locks)
    : create_missing_locks_(create_missing_locks) {}

Result<Fd> LockFileManager::OpenLockFile(const std::string& file_path) {
  return LockFileManager().OpenLockFileWithPolicy(file_path);
}

Result<Fd> LockFileManager::OpenLockFileWithPolicy(
    const std::string& file_path) const {
  Result<Fd, int> fd = Fd::Open(file_path, O_RDWR | O_CLOEXEC);
  if (fd.has_value()) {
    return std::move(*fd);
  }
  if (*fd.error() == ENOENT && create_missing_locks_) {
    const size_t slash = file_path.find_last_of('/');
    if (slash != std::string::npos && slash > 0) {
      CF_EXPECT(EnsureDirectoryExists(file_path.substr(0, slash), 0755));
    }
    return CF_EXPECT(Fd::Open(file_path, O_CREAT | O_RDWR | O_CLOEXEC, 0666));
  }
  return CF_EXPECTF(
      std::move(fd),
      "Failed to open lock file '{}' (ensure cuttlefish-host-resources "
      "service is running)",
      file_path);
}

Result<LockFile> LockFileManager::AcquireLock(
    const std::string& lock_file_path) {
  SharedFD fd = CF_EXPECT(OpenLockFileWithPolicy(lock_file_path));
  CF_EXPECT(fd->Flock(LOCK_EX));
  return LockFile(fd, lock_file_path);
}

Result<std::optional<LockFile>> LockFileManager::TryAcquireLock(
    const std::string& lock_file_path) {
  SharedFD fd = CF_EXPECT(OpenLockFileWithPolicy(lock_file_path));
  Result<void, int> flock_result = fd->Flock(LOCK_EX | LOCK_NB);
  if (flock_result) {
    return std::optional<LockFile>(LockFile(fd, lock_file_path));
  } else if (!flock_result && (*flock_result.error() == EWOULDBLOCK ||
                               *flock_result.error() == EAGAIN)) {
    return {};
  }
  CF_EXPECT(std::move(flock_result));
  return {};
}

// Replicates tempfile.gettempdir() in Python
std::string TempDir() {
  std::vector<std::string> try_dirs = {
      StringFromEnv("TMPDIR", ""),
      StringFromEnv("TEMP", ""),
      StringFromEnv("TMP", ""),
      "/tmp",
      "/var/tmp",
      "/usr/tmp",
  };
  for (const auto& try_dir : try_dirs) {
    if (DirectoryExists(try_dir)) {
      return try_dir;
    }
  }
  return CurrentDirectory();
}

}  // namespace cuttlefish
