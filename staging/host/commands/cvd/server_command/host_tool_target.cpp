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

#include "host/commands/cvd/server_command/host_tool_target.h"

#include <sys/stat.h>

#include <map>

#include "common/libs/utils/contains.h"
#include "common/libs/utils/files.h"
#include "common/libs/utils/result.h"
#include "common/libs/utils/subprocess.h"
#include "host/commands/cvd/common_utils.h"
#include "host/commands/cvd/server_command/flags_collector.h"

namespace cuttlefish {
namespace {

const std::map<std::string, std::vector<std::string>>& OpToBinsMap() {
  static const auto& map = *new std::map<std::string, std::vector<std::string>>{
      {"stop", {"cvd_internal_stop", "stop_cvd"}},
      {"stop_cvd", {"cvd_internal_stop", "stop_cvd"}},
      {"start", {"cvd_internal_start", "launch_cvd"}},
      {"launch_cvd", {"cvd_internal_start", "launch_cvd"}},
      {"status", {"cvd_internal_status", "cvd_status"}},
      {"cvd_status", {"cvd_internal_status", "cvd_status"}},
      {"restart", {"restart_cvd"}},
      {"powerwash", {"powerwash_cvd"}},
      {"suspend", {"snapshot_util_cvd"}},
      {"resume", {"snapshot_util_cvd"}},
      {"snapshot_take", {"snapshot_util_cvd"}},
  };
  return map;
}

}  // namespace

Result<HostToolTarget> HostToolTarget::Create(
    const std::string& artifacts_path) {
  std::string bin_dir_path = ConcatToString(artifacts_path, "/bin");
  std::unordered_map<std::string, OperationImplementation> op_to_impl_map;
  for (const auto& [op, candidates] : OpToBinsMap()) {
    for (const auto& bin_name : candidates) {
      const auto bin_path = ConcatToString(bin_dir_path, "/", bin_name);
      if (!FileExists(bin_path)) {
        continue;
      }
      op_to_impl_map[op] = OperationImplementation{.bin_name_ = bin_name};
      break;
    }
  }

  for (auto& [op, op_impl] : op_to_impl_map) {
    const std::string bin_path =
        ConcatToString(artifacts_path, "/bin/", op_impl.bin_name_);
    Command command(bin_path);
    command.AddParameter("--helpxml");
    // b/276497044
    command.UnsetFromEnvironment(kAndroidHostOut);
    command.AddEnvironmentVariable(kAndroidHostOut, artifacts_path);
    command.UnsetFromEnvironment(kAndroidSoongHostOut);
    command.AddEnvironmentVariable(kAndroidSoongHostOut, artifacts_path);

    std::string xml_str;
    RunWithManagedStdio(std::move(command), nullptr, std::addressof(xml_str),
                        nullptr);
    auto flags_opt = CollectFlagsFromHelpxml(xml_str);
    if (!flags_opt) {
      LOG(ERROR) << bin_path << " --helpxml failed.";
      continue;
    }
    auto flags = std::move(*flags_opt);
    for (auto& flag : flags) {
      op_impl.supported_flags_[flag->Name()] = std::move(flag);
    }
  }

  struct stat for_dir_time_stamp;
  time_t dir_time_stamp = 0;
  // we get dir time stamp, as the runtime libraries might be updated
  if (::stat(bin_dir_path.data(), &for_dir_time_stamp) == 0) {
    // if stat failed, use the smallest possible value, which is 0
    // in that way, the HostTool entry will be always updated on read request.
    dir_time_stamp = for_dir_time_stamp.st_mtime;
  }
  return HostToolTarget(artifacts_path, dir_time_stamp,
                        std::move(op_to_impl_map));
}

HostToolTarget::HostToolTarget(
    const std::string& artifacts_path, const time_t dir_time_stamp,
    std::unordered_map<std::string, OperationImplementation>&& op_to_impl_map)
    : artifacts_path_(artifacts_path),
      dir_time_stamp_(dir_time_stamp),
      op_to_impl_map_(std::move(op_to_impl_map)) {}

bool HostToolTarget::IsDirty() const {
  std::string bin_path = ConcatToString(artifacts_path_, "/bin");
  if (!DirectoryExists(bin_path)) {
    return true;
  }
  struct stat for_dir_time_stamp;
  if (::stat(bin_path.data(), &for_dir_time_stamp) != 0) {
    return true;
  }
  return dir_time_stamp_ != for_dir_time_stamp.st_mtime;
}

Result<FlagInfo> HostToolTarget::GetFlagInfo(
    const FlagInfoRequest& request) const {
  CF_EXPECT(Contains(op_to_impl_map_, request.operation_),
            "Operation \"" << request.operation_ << "\" is not supported.");
  auto& supported_flags =
      op_to_impl_map_.at(request.operation_).supported_flags_;
  CF_EXPECT(Contains(supported_flags, request.flag_name_));
  const auto& flag_uniq_ptr = supported_flags.at(request.flag_name_);
  FlagInfo copied(*flag_uniq_ptr);
  return copied;
}

Result<std::string> HostToolTarget::GetBinName(
    const std::string& operation) const {
  CF_EXPECT(Contains(op_to_impl_map_, operation),
            "Operation \"" << operation << "\" is not supported by "
                           << "the host tool target object at "
                           << artifacts_path_);
  return op_to_impl_map_.at(operation).bin_name_;
}

}  // namespace cuttlefish
