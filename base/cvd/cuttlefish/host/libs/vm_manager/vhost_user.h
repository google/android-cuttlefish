/*
 * Copyright (C) 2024 The Android Open Source Project
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

#include <string>
#include <string_view>

#include "common/libs/utils/subprocess.h"
#include "host/libs/config/cuttlefish_config.h"

namespace cuttlefish {
namespace vm_manager {

struct VhostUserDeviceCommands {
  Command device_cmd;
  Command device_logs_cmd;
  std::string socket_path;
};

Result<VhostUserDeviceCommands> VhostUserBlockDevice(
    const CuttlefishConfig& config, int num, std::string_view disk_path);

}  // namespace vm_manager
}  // namespace cuttlefish
