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

#include <fruit/fruit.h>

#include "common/libs/utils/contains.h"
#include "common/libs/utils/files.h"
#include "common/libs/utils/result.h"
#include "common/libs/utils/subprocess.h"
#include "host/commands/cvd/common_utils.h"
#include "host/commands/cvd/server_command/flags_collector.h"

namespace cuttlefish {

Result<HostToolTarget> HostToolTarget::Create(const std::string& artifacts_path,
                                              const std::string& start_bin) {
  std::string start_bin_path =
      ConcatToString(artifacts_path, "/bin/", start_bin);
  CF_EXPECT(FileExists(start_bin_path), start_bin_path << " does not exist.");

  Command command(start_bin_path);
  command.AddParameter("--helpxml");
  std::string xml_str;
  RunWithManagedStdio(std::move(command), nullptr, std::addressof(xml_str),
                      nullptr);
  auto flags_opt = CollectFlagsFromHelpxml(xml_str);
  CF_EXPECT(flags_opt != std::nullopt,
            "Parsing flags from " << start_bin_path << " --helpxml failed.");

  struct stat for_dir_time_stamp;
  time_t dir_time_stamp = 0;
  if (::stat(start_bin_path.data(), &for_dir_time_stamp) == 0) {
    // if stat failed, use the smallest possible value, which is 0
    // in that way, the HostTool entry will be always updated on read request.
    dir_time_stamp = for_dir_time_stamp.st_mtime;
  }
  return HostToolTarget(artifacts_path, start_bin, dir_time_stamp,
                        std::move(flags_opt.value()));
}

HostToolTarget::HostToolTarget(const std::string& artifacts_path,
                               const std::string& start_bin,
                               const time_t dir_time_stamp,
                               std::vector<FlagInfoPtr>&& flags)
    : artifacts_path_(artifacts_path),
      start_bin_(start_bin),
      dir_time_stamp_(dir_time_stamp) {
  for (int i = 0; i < flags.size(); i++) {
    supported_flags_[flags[i]->Name()] = std::move(flags[i]);
  }
}

bool HostToolTarget::IsDirty() const {
  std::string start_bin_path =
      ConcatToString(artifacts_path_, "/bin/", start_bin_);
  if (!FileExists(start_bin_path)) {
    return true;
  }
  struct stat for_dir_time_stamp;
  if (::stat(start_bin_path.data(), &for_dir_time_stamp) != 0) {
    return true;
  }
  return dir_time_stamp_ != for_dir_time_stamp.st_mtime;
}

Result<FlagInfo> HostToolTarget::GetFlagInfo(
    const std::string& flag_name) const {
  CF_EXPECT(Contains(supported_flags_, flag_name));
  const auto& flag_uniq_ptr = supported_flags_.at(flag_name);
  FlagInfo copied(*flag_uniq_ptr);
  return copied;
}

}  // namespace cuttlefish
