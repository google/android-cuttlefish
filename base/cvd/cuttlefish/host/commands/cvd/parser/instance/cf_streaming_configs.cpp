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

#include "cuttlefish/host/commands/cvd/parser/load_config.pb.h"
#include "host/commands/assemble_cvd/flags_defaults.h"
#include "host/commands/cvd/parser/cf_configs_common.h"

namespace cuttlefish {

using cvd::config::EnvironmentSpecification;
using cvd::config::Instance;

std::string DeviceId(const Instance& instance) {
  if (instance.streaming().has_device_id()) {
    return instance.streaming().device_id();
  } else {
    return CF_DEFAULTS_WEBRTC_DEVICE_ID;
  }
}

std::vector<std::string> GenerateStreamingFlags(
    const EnvironmentSpecification& cfg) {
  return std::vector<std::string>{
      GenerateInstanceFlag("webrtc_device_id", cfg, DeviceId),
  };
}

};  // namespace cuttlefish
