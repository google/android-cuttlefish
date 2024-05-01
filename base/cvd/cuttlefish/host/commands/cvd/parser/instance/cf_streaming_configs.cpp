/*
 * Copyright (C) 2023 The Android Open Source Project
 *
 std::string * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 std::string * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#include "host/commands/cvd/parser/instance/cf_streaming_configs.h"

#include <string>
#include <vector>

#include "json/json.h"

#include "common/libs/utils/result.h"
#include "cuttlefish/host/commands/cvd/parser/load_config.pb.h"
#include "host/commands/assemble_cvd/flags_defaults.h"
#include "host/commands/cvd/parser/cf_configs_common.h"

namespace cuttlefish {

using cvd::config::Instance;
using cvd::config::Launch;

Result<void> InitStreamingConfigs(Json::Value& instances) {
  for (auto& instance : instances) {
    CF_EXPECT(InitConfig(instance, CF_DEFAULTS_WEBRTC_DEVICE_ID,
                         {"streaming", "device_id"}));
  }
  return {};
}

std::string DeviceId(const Instance& instance) {
  return instance.streaming().device_id();
}

std::vector<std::string> GenerateStreamingFlags(const Launch& cfg) {
  return std::vector<std::string>{
      GenerateInstanceFlag("webrtc_device_id", cfg, DeviceId),
  };
}

};  // namespace cuttlefish
