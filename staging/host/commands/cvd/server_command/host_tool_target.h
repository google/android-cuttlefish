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

#pragma once

#include <sys/types.h>

#include <string>
#include <unordered_map>
#include <vector>

#include "common/libs/utils/result.h"
#include "host/commands/cvd/server_command/flags_collector.h"

namespace cuttlefish {

class HostToolTarget {
 public:
  // artifacts_path: ANDROID_HOST_OUT, or so
  static Result<HostToolTarget> Create(const std::string& artifacts_path,
                                       const std::string& start_bin);
  bool IsDirty() const;
  Result<FlagInfo> GetFlagInfo(const std::string& flag_name) const;
  bool HasField(const std::string& flag_name) const {
    return GetFlagInfo(flag_name).ok();
  }

 private:
  HostToolTarget(const std::string& artifacts_path,
                 const std::string& start_bin, const time_t dir_time_stamp_,
                 std::vector<FlagInfoPtr>&& flags);

  // time stamp on creation
  const std::string artifacts_path_;
  const std::string start_bin_;
  const time_t dir_time_stamp_;
  std::unordered_map<std::string, FlagInfoPtr> supported_flags_;
};

}  // namespace cuttlefish
