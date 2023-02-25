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
#include <regex>
#include <sstream>
#include <string>
#include <unordered_map>

#include <android-base/file.h>
#include <android-base/parseint.h>
#include <android-base/strings.h>
#include <fruit/fruit.h>

#include "common/libs/fs/shared_fd.h"
#include "common/libs/utils/contains.h"
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

static std::string DevicePatternString(
    const std::unordered_map<std::string, std::set<int>>& device_to_ids_map) {
  std::string device_pattern_str("^[[:space:]]*cvd-(");
  for (const auto& [key, _] : device_to_ids_map) {
    device_pattern_str.append(key).append("|");
  }
  if (!device_to_ids_map.empty()) {
    *device_pattern_str.rbegin() = ')';
  }
  device_pattern_str.append("-[0-9]+");
  return device_pattern_str;
}

struct TypeAndId {
  std::string device_type;
  int id;
};
// call this if the line is a network device line
static Result<TypeAndId> ParseMatchedLine(
    const std::smatch& device_string_match) {
  std::string device_string = *device_string_match.begin();
  auto tokens = android::base::Tokenize(device_string, "-");
  CF_EXPECT_GE(tokens.size(), 3);
  const auto cvd = tokens.front();
  int id = 0;
  CF_EXPECT(android::base::ParseInt(tokens.back(), &id));
  // '-'.join(tokens[1:-1])
  tokens.pop_back();
  tokens.erase(tokens.begin());
  const auto device_type = android::base::Join(tokens, "-");
  return TypeAndId{.device_type = device_type, .id = id};
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
  std::unordered_map<std::string, std::set<int>> device_to_ids_map{
      {"etap", std::set<int>{}},
      {"mtap", std::set<int>{}},
      {"wtap", std::set<int>{}},
      {"wifiap", std::set<int>{}},
  };
  // "^[[:space:]]*cvd-(etap|mtap|wtap|wifiap)-[0-9]+"
  std::string device_pattern_str = DevicePatternString(device_to_ids_map);

  std::regex device_pattern(device_pattern_str);
  for (const auto& line : lines) {
    std::smatch device_string_match;
    if (!std::regex_search(line, device_string_match, device_pattern)) {
      continue;
    }
    const auto [device_type, id] =
        CF_EXPECT(ParseMatchedLine(device_string_match));
    CF_EXPECT(Contains(device_to_ids_map, device_type));
    device_to_ids_map[device_type].insert(id);
  }

  std::set<int> result{device_to_ids_map["etap"]};  // any set except "wifiap"
  for (const auto& [device_type, id_set] : device_to_ids_map) {
    /*
     * b/2457509
     *
     * Until the debian host packages are sufficiently up-to-date, the wifiap
     * devices wouldn't show up in /proc/net/dev.
     */
    if (device_type == "wifiap" && id_set.empty()) {
      continue;
    }
    std::set<int> tmp;
    std::set_intersection(result.begin(), result.end(), id_set.begin(),
                          id_set.end(), std::inserter(tmp, tmp.begin()));
    result = std::move(tmp);
  }
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
