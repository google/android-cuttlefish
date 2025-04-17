//
// Copyright (C) 2025 The Android Open Source Project
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

#pragma once

#include "cuttlefish/host/commands/run_cvd/launch/sensors_socket_pair.h"
#include "cuttlefish/host/libs/config/command_source.h"
#include "cuttlefish/host/libs/config/cuttlefish_config.h"
#include "cuttlefish/host/libs/config/kernel_log_pipe_provider.h"

namespace cuttlefish {

Result<MonitorCommand> SensorsSimulator(
    const CuttlefishConfig::InstanceSpecific& instance,
    AutoSensorsSocketPair::Type& sensors_socket_pair,
    KernelLogPipeProvider& kernel_log_pipe_provider);

}  // namespace cuttlefish
