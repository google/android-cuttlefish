//
// Copyright (C) 2023 The Android Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "host/commands/run_cvd/launch/launch.h"

#include <optional>
#include <string>
#include <utility>

#include <fruit/fruit.h>

#include "host/libs/config/command_source.h"
#include "host/libs/config/cuttlefish_config.h"
#include "host/libs/config/known_paths.h"

namespace cuttlefish {

std::optional<MonitorCommand> AutomotiveProxyService(
    const CuttlefishConfig& config) {
  if (!config.enable_automotive_proxy()) {
    return {};
  }
  // Create the Automotive Proxy command
  return Command(AutomotiveProxyBinary())
      .AddParameter(
          DefaultHostArtifactsPath("etc/automotive/proxy_config.json"));
}

}  // namespace cuttlefish
