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

#include "cuttlefish/host/commands/cvd/cli/commands/host_tool_target.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <android-base/strings.h>
#include <fmt/core.h>

#include "cuttlefish/common/libs/utils/files.h"
#include "cuttlefish/common/libs/utils/result.h"
#include "cuttlefish/common/libs/utils/subprocess.h"
#include "cuttlefish/common/libs/utils/subprocess_managed_stdio.h"
#include "cuttlefish/host/commands/cvd/utils/common.h"
#include "cuttlefish/host/commands/cvd/utils/flags_collector.h"

namespace cuttlefish {
namespace {

Result<std::vector<FlagInfo>> GetSupportedFlags(
    const std::string& artifacts_path, const std::string bin_name) {
  auto bin_path = fmt::format("{}/{}", artifacts_path, bin_name);
  Command command(bin_path);
  command.AddParameter("--helpxml");
  // b/276497044
  command.UnsetFromEnvironment(kAndroidHostOut);
  command.AddEnvironmentVariable(kAndroidHostOut, artifacts_path);
  command.UnsetFromEnvironment(kAndroidSoongHostOut);
  command.AddEnvironmentVariable(kAndroidSoongHostOut, artifacts_path);

  std::string xml_str;
  std::string err_out;
  RunWithManagedStdio(std::move(command), nullptr, std::addressof(xml_str),
                      std::addressof(err_out));
  auto flags_opt = CollectFlagsFromHelpxml(xml_str);
  CF_EXPECTF(flags_opt.has_value(), " --helpxml failed: {}", err_out);
  return std::move(*flags_opt);
}

}  // namespace

HostToolTarget::HostToolTarget(const std::string& artifacts_path)
    : artifacts_path_(artifacts_path) {}

Result<FlagInfo> HostToolTarget::GetFlagInfo(
    const std::string& bin_name, const std::string& flag_name) const {
  std::vector<FlagInfo> flags = CF_EXPECTF(
      GetSupportedFlags(artifacts_path_, bin_name),
      "Failed to obtain supported flags for the '{}' tool", bin_name);
  for (FlagInfo& flag : flags) {
    if (flag.Name() == flag_name) {
      return flag;
    }
  }
  return CF_ERRF("Flag '{}' not supported by the '{}' tool", flag_name,
                 bin_name);
}

Result<std::string> HostToolTarget::GetStartBinName() const {
  return CF_EXPECT(GetBinName({"cvd_internal_start", "launch_cvd"}));
}

Result<std::string> HostToolTarget::GetStopBinName() const {
  return CF_EXPECT(GetBinName({"cvd_internal_stop", "stop_cvd"}));
}

Result<std::string> HostToolTarget::GetStatusBinName() const {
  return CF_EXPECT(GetBinName({"cvd_internal_status", "cvd_status"}));
}

Result<std::string> HostToolTarget::GetRestartBinPath() const {
  std::string bin_name = CF_EXPECT(GetBinName({"restart_cvd"}));
  return fmt::format("{}/bin/{}", artifacts_path_, bin_name);
}

Result<std::string> HostToolTarget::GetPowerwashBinPath() const {
  std::string bin_name = CF_EXPECT(GetBinName({"powerwash_cvd"}));
  return fmt::format("{}/bin/{}", artifacts_path_, bin_name);
}

Result<std::string> HostToolTarget::GetPowerBtnBinPath() const {
  std::string bin_name = CF_EXPECT(GetBinName({"powerbtn_cvd"}));
  return fmt::format("{}/bin/{}", artifacts_path_, bin_name);
}

Result<std::string> HostToolTarget::GetSnapshotBinName() const {
  return CF_EXPECT(GetBinName({"snapshot_util_cvd"}));
}

Result<std::string> HostToolTarget::GetBinName(
    const std::vector<std::string>& alternatives) const {
  for (const auto& bin_name : alternatives) {
    const auto bin_path = fmt::format("{}/bin/{}", artifacts_path_, bin_name);
    if (FileExists(bin_path)) {
      return bin_name;
    }
  }
  return CF_ERRF("'{}/bin/' does not contain any of '[{}]'.", artifacts_path_,
                 android::base::Join(alternatives, ", "));
}

}  // namespace cuttlefish
