/*
 * Copyright (C) 2019 The Android Open Source Project
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
#include "cuttlefish/host/commands/assemble_cvd/flags/mcu_config_path.h"

#include <stddef.h>

#include <string>
#include <utility>
#include <vector>

#include <android-base/file.h>
#include <android-base/strings.h>
#include "absl/strings/str_split.h"
#include <gflags/gflags.h>
#include <json/value.h>

#include "cuttlefish/common/libs/utils/files.h"
#include "cuttlefish/common/libs/utils/json.h"
#include "cuttlefish/host/commands/assemble_cvd/flags_defaults.h"
#include "cuttlefish/host/libs/config/config_utils.h"
#include "cuttlefish/result/result.h"

DEFINE_string(mcu_config_path, CF_DEFAULTS_MCU_CONFIG_PATH,
              "configuration file for the MCU emulator");

namespace cuttlefish {

using android::base::ReadFileToString;

McuConfigPathFlag McuConfigPathFlag::FromGlobalGflags() {
  std::string default_path = DefaultHostArtifactsPath("etc/mcu_config.json");
  if (!CanAccess(default_path, R_OK)) {
    default_path = CF_DEFAULTS_MCU_CONFIG_PATH;
  }

  gflags::CommandLineFlagInfo flag_info =
      gflags::GetCommandLineFlagInfoOrDie("mcu_config_path");

  std::string mcu_config_path_flag =
      flag_info.is_default ? default_path : flag_info.current_value;

  std::vector<std::string> paths =
      absl::StrSplit(mcu_config_path_flag, ',');

  if (paths.empty()) {
    paths.emplace_back("");
  }

  return McuConfigPathFlag(std::move(paths));
}

std::string McuConfigPathFlag::PathForIndex(size_t argument_index) const {
  if (argument_index >= mcu_config_paths_.size()) {
    argument_index = 0;
  }
  return mcu_config_paths_[argument_index];
}

Result<Json::Value> McuConfigPathFlag::JsonForIndex(
    size_t argument_index) const {
  std::string mcu_cfg_path = PathForIndex(argument_index);
  if (mcu_cfg_path.empty()) {
    return {};
  }
  CF_EXPECT(FileExists(mcu_cfg_path), "MCU config file does not exist");

  std::string content;
  CF_EXPECTF(ReadFileToString(mcu_cfg_path, &content,
                              /* follow_symlinks */ true),
             "Failed to read mcu config file '{}'", mcu_cfg_path);

  return CF_EXPECTF(ParseJson(content), "Failed to parse '{}'", mcu_cfg_path);
}

McuConfigPathFlag::McuConfigPathFlag(std::vector<std::string> mcu_config_paths)
    : mcu_config_paths_(std::move(mcu_config_paths)) {}

}  // namespace cuttlefish
