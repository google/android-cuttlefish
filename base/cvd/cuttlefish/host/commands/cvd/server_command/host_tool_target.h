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

#include "common/libs/utils/result.h"
#include "host/commands/cvd/server_command/flags_collector.h"

namespace cuttlefish {

class HostToolTarget {
 public:
  // artifacts_path: ANDROID_HOST_OUT, or so
  HostToolTarget(const std::string& artifacts_path);

  Result<FlagInfo> GetFlagInfo(const std::string& bin_name,
                               const std::string& flag_name) const;

  Result<std::string> GetStartBinName() const;
  Result<std::string> GetStopBinName() const;
  Result<std::string> GetStatusBinName() const;
  Result<std::string> GetRestartBinName() const;
  Result<std::string> GetPowerwashBinName() const;
  Result<std::string> GetPowerBtnBinName() const;
  Result<std::string> GetSnapshotBinName() const;

 private:
  Result<std::string> GetBinName(
      const std::vector<std::string>& alternatives) const;
  const std::string artifacts_path_;
};

}  // namespace cuttlefish
