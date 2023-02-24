/*
 * Copyright (C) 2022 The Android Open Source Project
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

#include "host/commands/cvd/instance_lock.h"

#include <sys/file.h>

#include <algorithm>
#include <sstream>
#include <string>

#include <android-base/file.h>
#include <android-base/strings.h>
#include <fruit/fruit.h>

#include "common/libs/fs/shared_fd.h"
#include "common/libs/utils/environment.h"
#include "common/libs/utils/files.h"
#include "common/libs/utils/result.h"

namespace cuttlefish {

InstanceLockFile::InstanceLockFile(SharedFD fd, int instance_num)
    : fd_(fd), instance_num_(instance_num) {}

int InstanceLockFile::Instance() const { return instance_num_; }

Result<InUseState> InstanceLockFile::Status() const {
  CF_EXPECT(fd_->LSeek(0, SEEK_SET) == 0, fd_->StrError());
  char state_char = static_cast<char>(InUseState::kNotInUse);
  CF_EXPECT(fd_->Read(&state_char, 1) >= 0, fd_->StrError());
  switch (state_char) {
    case static_cast<char>(InUseState::kInUse):
      return InUseState::kInUse;
    case static_cast<char>(InUseState::kNotInUse):
      return InUseState::kNotInUse;
    default:
      return CF_ERR("Unexpected state value \"" << state_char << "\"");
  }
}

Result<void> InstanceLockFile::Status(InUseState state) {
  CF_EXPECT(fd_->LSeek(0, SEEK_SET) == 0, fd_->StrError());
  char state_char = static_cast<char>(state);
  CF_EXPECT(fd_->Write(&state_char, 1) == 1, fd_->StrError());
  return {};
}

bool InstanceLockFile::operator<(const InstanceLockFile& other) const {
  if (instance_num_ != other.instance_num_) {
    return instance_num_ < other.instance_num_;
  }
  return fd_ < other.fd_;
}

InstanceLockFileManager::InstanceLockFileManager() {}

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

static Result<SharedFD> OpenLockFile(int instance_num) {
  std::stringstream path;
  path << TempDir() << "/acloud_cvd_temp/";
  CF_EXPECT(EnsureDirectoryExists(path.str()));
  path << "local-instance-" << instance_num << ".lock";
  auto fd = SharedFD::Open(path.str(), O_CREAT | O_RDWR, 0666);
  CF_EXPECT(fd->IsOpen(), "open(\"" << path.str() << "\"): " << fd->StrError());
  return fd;
}

Result<InstanceLockFile> InstanceLockFileManager::AcquireLock(
    int instance_num) {
  auto fd = CF_EXPECT(OpenLockFile(instance_num));
  CF_EXPECT(fd->Flock(LOCK_EX));
  return InstanceLockFile(fd, instance_num);
}

Result<std::set<InstanceLockFile>> InstanceLockFileManager::AcquireLocks(
    const std::set<int>& instance_nums) {
  std::set<InstanceLockFile> locks;
  for (const auto& num : instance_nums) {
    locks.emplace(CF_EXPECT(AcquireLock(num)));
  }
  return locks;
}

Result<std::optional<InstanceLockFile>> InstanceLockFileManager::TryAcquireLock(
    int instance_num) {
  auto fd = CF_EXPECT(OpenLockFile(instance_num));
  auto flock_result = fd->Flock(LOCK_EX | LOCK_NB);
  if (flock_result.ok()) {
    return InstanceLockFile(fd, instance_num);
    // TODO(schuffelen): Include the error code in the Result
  } else if (!flock_result.ok() && fd->GetErrno() == EWOULDBLOCK) {
    return {};
  }
  CF_EXPECT(std::move(flock_result));
  return {};
}

Result<std::set<InstanceLockFile>> InstanceLockFileManager::TryAcquireLocks(
    const std::set<int>& instance_nums) {
  std::set<InstanceLockFile> locks;
  for (const auto& num : instance_nums) {
    auto lock = CF_EXPECT(TryAcquireLock(num));
    if (lock) {
      locks.emplace(std::move(*lock));
    }
  }
  return locks;
}

Result<std::vector<InstanceLockFile>>
InstanceLockFileManager::LockAllAvailable() {
  if (!all_instance_nums_) {
    all_instance_nums_ = CF_EXPECT(FindPotentialInstanceNumsFromNetDevices());
  }

  std::vector<InstanceLockFile> acquired_lock_files;
  for (const auto num : *all_instance_nums_) {
    auto lock = CF_EXPECT(TryAcquireLock(num));
    if (!lock) {
      continue;
    }
    auto status = CF_EXPECT(lock->Status());
    if (status != InUseState::kNotInUse) {
      continue;
    }
    acquired_lock_files.emplace_back(std::move(*lock));
  }
  return acquired_lock_files;
}

Result<std::set<int>>
InstanceLockFileManager::FindPotentialInstanceNumsFromNetDevices() {
  // Estimate this by looking at available tap devices
  // clang-format off
  /** Sample format:
Inter-|   Receive                                                |  Transmit
 face |bytes    packets errs drop fifo frame compressed multicast|bytes    packets errs drop fifo colls carrier compressed
cvd-wtap-02:       0       0    0    0    0     0          0         0        0       0    0    0    0     0       0          0
  */
  // clang-format on
  static constexpr char kPath[] = "/proc/net/dev";
  std::string proc_net_dev;
  using android::base::ReadFileToString;
  CF_EXPECT(ReadFileToString(kPath, &proc_net_dev, /* follow_symlinks */ true));

  auto lines = android::base::Split(proc_net_dev, "\n");
  std::set<int> etaps, mtaps, wtaps, wifiaps;
  for (const auto& line : lines) {
    std::set<int>* tap_set = nullptr;
    if (android::base::StartsWith(line, "cvd-etap-")) {
      tap_set = &etaps;
    } else if (android::base::StartsWith(line, "cvd-mtap-")) {
      tap_set = &mtaps;
    } else if (android::base::StartsWith(line, "cvd-wtap-")) {
      tap_set = &wtaps;
    } else if (android::base::StartsWith(line, "cvd-wifiap-")) {
      tap_set = &wifiaps;
    } else {
      continue;
    }
    tap_set->insert(std::stoi(line.substr(std::string{"cvd-etap-"}.size())));
  }
  std::set<int> emtaps;
  std::set_intersection(etaps.begin(), etaps.end(), mtaps.begin(), mtaps.end(),
                        std::inserter(emtaps, emtaps.begin()));
  std::set<int> emwtaps;
  std::set_intersection(emtaps.begin(), emtaps.end(), wtaps.begin(),
                        wtaps.end(), std::inserter(emwtaps, emwtaps.begin()));
  std::set<int> result;
  std::set_intersection(emwtaps.begin(), emwtaps.end(), wifiaps.begin(),
                        wifiaps.end(), std::inserter(result, result.begin()));
  return result;
}

Result<std::optional<InstanceLockFile>>
InstanceLockFileManager::TryAcquireUnusedLock() {
  if (!all_instance_nums_) {
    all_instance_nums_ = CF_EXPECT(FindPotentialInstanceNumsFromNetDevices());
  }

  for (const auto num : *all_instance_nums_) {
    auto lock = CF_EXPECT(TryAcquireLock(num));
    if (lock && CF_EXPECT(lock->Status()) == InUseState::kNotInUse) {
      return std::move(*lock);
    }
  }
  return {};
}

}  // namespace cuttlefish
