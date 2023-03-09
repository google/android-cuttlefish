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

#include "host/commands/cvd/server_command/host_tool_target_manager.h"

#include "common/libs/utils/contains.h"
#include "common/libs/utils/files.h"
#include "host/commands/cvd/common_utils.h"

namespace cuttlefish {

fruit::Component<OperationToBinsMap> OperationToBinsMapComponent() {
  return fruit::createComponent().registerProvider(
      [](void) -> OperationToBinsMap {
        OperationToBinsMap op_to_possible_bins_map;
        op_to_possible_bins_map["stop"] =
            std::vector<std::string>{"cvd_internal_stop", "stop_cvd"};
        op_to_possible_bins_map["start"] =
            std::vector<std::string>{"cvd_internal_start", "launch_cvd"};
        op_to_possible_bins_map["status"] =
            std::vector<std::string>{"cvd_internal_status", "cvd_status"};
        return op_to_possible_bins_map;
      });
}

}  // namespace cuttlefish
