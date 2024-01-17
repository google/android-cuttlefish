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

#include "host/commands/cvd/lock_file.h"

#include <sys/file.h>

#include <algorithm>
#include <cstring>
#include <sstream>
#include <vector>

#include <android-base/file.h>
#include <android-base/strings.h>

#include "common/libs/fs/shared_fd.h"
#include "common/libs/utils/environment.h"
#include "common/libs/utils/files.h"
#include "common/libs/utils/result.h"

namespace cuttlefish {
namespace cvd_impl {

static Result<void> SetStatus(SharedFD& fd, const InUseState state) {
  CF_EXPECT(fd->LSeek(0, SEEK_SET) == 0, fd->StrError());
  char state_char = static_cast<char>(state);
  CF_EXPECT(fd->Write(&state_char, 1) == 1, fd->StrError());
  return {};
}

LockFile::LockFileReleaser::LockFileReleaser(const SharedFD& fd,
                                             const std::string& lock_file_path)
    : flocked_file_fd_(fd), lock_file_path_(lock_file_path) {}

LockFile::LockFileReleaser::~LockFileReleaser() {
  if (!flocked_file_fd_->IsOpen()) {
    LOG(ERROR) << "SharedFD to " << lock_file_path_
               << " is closed and unable to un-flock()";
    return;
  }
  auto funlock_result = flocked_file_fd_->Flock(LOCK_UN | LOCK_NB);
  if (!funlock_result.ok()) {
    LOG(ERROR) << "Unlock the \"" << lock_file_path_
               << "\" failed: " << funlock_result.error().Trace();
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
  CF_EXPECT(fd_->Read(&state_char, 1) >= 0, fd_->StrError());
  switch (state_char) {
    case static_cast<char>(InUseState::kInUse):
      return InUseState::kInUse;
    case static_cast<char>(InUseState::kNotInUse):
      return InUseState::kNotInUse;
    default:
      return CF_ERRF("Unexpected state value \"{}\"", state_char);
  }
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

Result<SharedFD> LockFileManager::OpenLockFile(const std::string& file_path) {
  auto parent_dir = android::base::Dirname(file_path);
  CF_EXPECT(EnsureDirectoryExists(parent_dir));
  auto fd = SharedFD::Open(file_path.data(), O_CREAT | O_RDWR, 0666);
  int result = chmod(file_path.c_str(), 0666);
  if (result) {
    LOG(DEBUG) << "failed: chmod 666 " << file_path;
  }
  result = chmod(parent_dir.c_str(), 0755);
  if (result) {
    LOG(DEBUG) << "failed: chmod 755 " << parent_dir;
  }
  CF_EXPECTF(fd->IsOpen(), "open(\"{}\"): {}", file_path, fd->StrError());
  return fd;
}

Result<LockFile> LockFileManager::AcquireLock(
    const std::string& lock_file_path) {
  auto fd = CF_EXPECT(OpenLockFile(lock_file_path));
  CF_EXPECT(fd->Flock(LOCK_EX));
  return LockFile(fd, lock_file_path);
}

Result<std::set<LockFile>> LockFileManager::AcquireLocks(
    const std::set<std::string>& lock_file_paths) {
  std::set<LockFile> locks;
  for (const auto& lock_file_path : lock_file_paths) {
    locks.emplace(CF_EXPECT(AcquireLock(lock_file_path)));
  }
  return locks;
}

Result<std::optional<LockFile>> LockFileManager::TryAcquireLock(
    const std::string& lock_file_path) {
  auto fd = CF_EXPECT(OpenLockFile(lock_file_path));
  auto flock_result = fd->Flock(LOCK_EX | LOCK_NB);
  if (flock_result.ok()) {
    return std::optional<LockFile>(LockFile(fd, lock_file_path));
    // TODO(schuffelen): Include the error code in the Result
  } else if (!flock_result.ok() && fd->GetErrno() == EWOULDBLOCK) {
    return {};
  }
  CF_EXPECT(std::move(flock_result));
  return {};
}

Result<std::set<LockFile>> LockFileManager::TryAcquireLocks(
    const std::set<std::string>& lock_file_paths) {
  std::set<LockFile> locks;
  for (const auto& lock_file_path : lock_file_paths) {
    auto lock = CF_EXPECT(TryAcquireLock(lock_file_path));
    if (lock) {
      locks.emplace(std::move(*lock));
    }
  }
  return locks;
}

}  // namespace cvd_impl

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
