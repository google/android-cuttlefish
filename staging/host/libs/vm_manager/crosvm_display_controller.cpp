/*
 * Copyright (C) 2025 The Android Open Source Project
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

#include "host/libs/vm_manager/crosvm_display_controller.h"

#include <android-base/logging.h>
#include <cstddef>
#include <iostream>
#include <string>
#include <vector>
#include "common/libs/utils/result.h"
#include "common/libs/utils/subprocess.h"
#include "host/libs/config/cuttlefish_config.h"

namespace cuttlefish {
namespace vm_manager {

Result<CrosvmDisplayController> GetCrosvmDisplayController() {
  auto config = CuttlefishConfig::Get();
  if (!config) {
    return CF_ERR("Failed to get Cuttlefish config.");
  }
  auto vm_manager = config->vm_manager();
  if (vm_manager != VmmMode::kCrosvm) {
    LOG(ERROR) << "Expected vm_manager is kCrosvm but " << vm_manager;
    return CF_ERR(
        "CrosvmDisplayController is only available when VmmMode is kCrosvm");
  }
  return CrosvmDisplayController(config);
}

Result<int> CrosvmDisplayController::Add(
    const int instance_num,
    const std::vector<CuttlefishConfig::DisplayConfig>& display_configs) const {
  std::vector<std::string> command_args;
  command_args.push_back("add-displays");

  for (const auto& display_config : display_configs) {
    const std::string w = std::to_string(display_config.width);
    const std::string h = std::to_string(display_config.height);
    const std::string dpi = std::to_string(display_config.dpi);
    const std::string rr = std::to_string(display_config.refresh_rate_hz);

    const std::string add_display_flag =
        "--gpu-display=" + android::base::Join(
                               std::vector<std::string>{
                                   "mode=windowed[" + w + "," + h + "]",
                                   "dpi=[" + dpi + "," + dpi + "]",
                                   "refresh-rate=" + rr,
                               },
                               ",");

    command_args.push_back(add_display_flag);
  }

  return RunCrosvmDisplayCommand(instance_num, command_args, NULL);
}

Result<int> CrosvmDisplayController::Remove(
    const int instance_num, const std::vector<std::string> display_ids) const {
  std::vector<std::string> command_args;
  command_args.push_back("remove-displays");

  for (const auto& display_id : display_ids) {
    command_args.push_back("--display-id=" + display_id);
  }

  return RunCrosvmDisplayCommand(instance_num, command_args, NULL);
}

Result<std::string> CrosvmDisplayController::List(const int instance_num) {
  std::string out;
  CF_EXPECT(RunCrosvmDisplayCommand(instance_num, {"list-displays"}, &out));
  return out;
}

Result<int> CrosvmDisplayController::RunCrosvmDisplayCommand(
    const int instance_num, const std::vector<std::string>& args,
    std::string* stdout_str) const {
  // TODO(b/260649774): Consistent executable API for selecting an instance
  const CuttlefishConfig::InstanceSpecific instance =
      config_->ForInstance(instance_num);

  const std::string crosvm_binary_path = instance.crosvm_binary();
  const std::string crosvm_control_path = instance.CrosvmSocketPath();

  Command command(crosvm_binary_path);
  command.AddParameter("gpu");
  for (const std::string& arg : args) {
    command.AddParameter(arg);
  }
  command.AddParameter(crosvm_control_path);

  std::string err;
  auto ret = RunWithManagedStdio(std::move(command), NULL, stdout_str, &err);
  if (ret != 0) {
    LOG(ERROR) << "Failed to run crosvm display command: ret code: " << ret
               << "\n"
               << err << std::endl;
    return CF_ERRF("Failed to run crosvm display command: ret code: {}", ret);
  }

  return 0;
}

}  // namespace vm_manager
}  // namespace cuttlefish
