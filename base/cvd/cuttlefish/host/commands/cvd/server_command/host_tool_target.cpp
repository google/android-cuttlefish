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
#include <string>
#include <vector>

#include <android-base/strings.h>
#include <fmt/format.h>

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
      {"powerbtn", {"powerbtn_cvd"}},
      {"suspend", {"snapshot_util_cvd"}},
      {"resume", {"snapshot_util_cvd"}},
      {"snapshot_take", {"snapshot_util_cvd"}},
  };
  return map;
}

Result<std::vector<FlagInfoPtr>> GetSupportedFlags(
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
    const std::string& operation, const std::string& flag_name) const {
  std::string bin_name = CF_EXPECTF(GetBinName(operation),
                                    "Operation '{}' not supported", operation);
  std::vector<FlagInfoPtr> flags = CF_EXPECTF(
      GetSupportedFlags(artifacts_path_, bin_name),
      "Failed to obtain supported flags for the '{}' tool", bin_name);
  for (auto& flag : flags) {
    if (flag->Name() == flag_name) {
      return *flag.release();
    }
  }
  return CF_ERRF("Flag '{}' not supported by the '{}' tool", flag_name,
                 bin_name);
}

Result<std::string> HostToolTarget::GetBinName(
    const std::string& operation) const {
  auto operation_find = OpToBinsMap().find(operation);
  CF_EXPECTF(operation_find != OpToBinsMap().end(),
             "Operation '{}' not supported", operation);
  auto& candidates = operation_find->second;
  for (const auto& bin_name : candidates) {
    const auto bin_path = fmt::format("{}/bin/{}", artifacts_path_, bin_name);
    if (FileExists(bin_path)) {
      return bin_name;
    }
  }
  return CF_ERRF(
      "No suitable binary found for operation '{}' in '{}'. Looked for '{}'.",
      operation, artifacts_path_, android::base::Join(candidates, ", "));
}

}  // namespace cuttlefish
