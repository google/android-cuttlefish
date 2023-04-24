/*
 * Copyright (C) 2022 The Android Open Source Project
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

#include "host/commands/cvd/parser/cf_configs_instances.h"

#include <android-base/logging.h>
#include <iostream>

#include "host/commands/cvd/parser/cf_configs_common.h"
#include "host/commands/cvd/parser/instance/cf_boot_configs.h"
#include "host/commands/cvd/parser/instance/cf_disk_configs.h"
#include "host/commands/cvd/parser/instance/cf_graphics_configs.h"
#include "host/commands/cvd/parser/instance/cf_metrics_configs.h"
#include "host/commands/cvd/parser/instance/cf_security_configs.h"
#include "host/commands/cvd/parser/instance/cf_vm_configs.h"

namespace cuttlefish {

void InitInstancesConfigs(Json::Value& root) {
  InitVmConfigs(root);
  InitDiskConfigs(root);
  InitBootConfigs(root);
  InitSecurityConfigs(root);
  InitGraphicsConfigs(root);
}

std::vector<std::string> GenerateInstancesFlags(const Json::Value& root) {
  std::vector<std::string> result = GenerateVmFlags(root);
  result = MergeResults(result, GenerateDiskFlags(root));
  if (!GENERATE_MVP_FLAGS_ONLY) {
    result = MergeResults(result, GenerateBootFlags(root));
  }
  result = MergeResults(result, GenerateSecurityFlags(root));
  result = MergeResults(result, GenerateGraphicsFlags(root));
  result = MergeResults(result, GenerateMetricsFlags(root));

  return result;
}

}  // namespace cuttlefish
