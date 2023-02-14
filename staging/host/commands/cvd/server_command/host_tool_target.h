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
  using OperationToBinsMap =
      std::unordered_map<std::string, std::vector<std::string>>;
  struct FlagInfoRequest {
    std::string operation_;
    std::string flag_name_;
  };
  // artifacts_path: ANDROID_HOST_OUT, or so
  static Result<HostToolTarget> Create(
      const std::string& artifacts_path,
      const OperationToBinsMap& supported_operations);

  bool IsDirty() const;
  Result<FlagInfo> GetFlagInfo(const FlagInfoRequest& request) const;
  bool HasField(const FlagInfoRequest& request) const {
    return GetFlagInfo(request).ok();
  }
  Result<std::string> GetBinName(const std::string& operation) const;

 private:
  using SupportedFlagMap = std::unordered_map<std::string, FlagInfoPtr>;
  struct OperationImplementation {
    std::string bin_name_;
    SupportedFlagMap supported_flags_;
  };
  HostToolTarget(const std::string& artifacts_path, const time_t dir_time_stamp,
                 std::unordered_map<std::string, OperationImplementation>&&
                     op_to_impl_map);

  // time stamp on creation
  const std::string artifacts_path_;
  const time_t dir_time_stamp_;
  // map from "start", "stop", etc, to "cvd_internal_start", "stop_cvd", etc
  // with the supported flags if those implementation offers --helpxml.
  std::unordered_map<std::string, OperationImplementation> op_to_impl_map_;
};

}  // namespace cuttlefish
