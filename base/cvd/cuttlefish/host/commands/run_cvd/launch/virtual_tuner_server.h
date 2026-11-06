/*
 * Copyright (C) 2026 The Android Open Source Project
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

#include <optional>
#include <string>

#include "cuttlefish/host/libs/config/cuttlefish_config.h"
#include "cuttlefish/host/libs/feature/command_source.h"
#include "cuttlefish/result/result.h"

namespace cuttlefish {

// Returns true if audio is enabled and the guest audio config declares a
// virtual_tuner stream on any PCM device.
bool HasVirtualTunerStream(const CuttlefishConfig::InstanceSpecific& instance);

// Returns the path of the host-local socket that carries tuner PCM from
// virtual_tuner_daemon to webRTC. Both ends are host processes, so it lives in
// the instance's internal UDS directory.
std::string VirtualTunerPcmSocketPath(
    const CuttlefishConfig::InstanceSpecific& instance);

// Returns the command that starts virtual_tuner_daemon, or nullopt if the
// guest has no virtual_tuner stream. Creates the PCM listening socket that
// webRTC connects to.
Result<std::optional<MonitorCommand>> VirtualTunerServer(
    const CuttlefishConfig::InstanceSpecific& instance);

}  // namespace cuttlefish
