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

#include "common/libs/utils/result.h"
#include "host/commands/cvd/parser/cf_configs_common.h"
#include "host/commands/cvd/parser/instance/cf_boot_configs.h"
#include "host/commands/cvd/parser/instance/cf_disk_configs.h"
#include "host/commands/cvd/parser/instance/cf_graphics_configs.h"
#include "host/commands/cvd/parser/instance/cf_metrics_configs.h"
#include "host/commands/cvd/parser/instance/cf_security_configs.h"
#include "host/commands/cvd/parser/instance/cf_vm_configs.h"

namespace cuttlefish {

Result<void> InitInstancesConfigs(Json::Value& root) {
  CF_EXPECT(InitVmConfigs(root));
  CF_EXPECT(InitDiskConfigs(root));
  CF_EXPECT(InitBootConfigs(root));
  CF_EXPECT(InitSecurityConfigs(root));
  CF_EXPECT(InitGraphicsConfigs(root));
  return {};
}

Result<std::vector<std::string>> GenerateInstancesFlags(
    const Json::Value& root) {
  std::vector<std::string> result = CF_EXPECT(GenerateVmFlags(root));
  result = MergeResults(result, CF_EXPECT(GenerateDiskFlags(root)));
  result = MergeResults(result, CF_EXPECT(GenerateBootFlags(root)));
  result = MergeResults(result, CF_EXPECT(GenerateSecurityFlags(root)));
  result = MergeResults(result, CF_EXPECT(GenerateGraphicsFlags(root)));
  result = MergeResults(result, GenerateMetricsFlags(root));

  return result;
}

}  // namespace cuttlefish
