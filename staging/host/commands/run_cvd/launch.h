//
// Copyright (C) 2019 The Android Open Source Project
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

#include <string>
#include <vector>

#include "common/libs/fs/shared_fd.h"
#include "common/libs/utils/subprocess.h"
#include "host/libs/config/cuttlefish_config.h"

namespace cuttlefish {

struct KernelLogMonitorData {
  std::vector<SharedFD> pipes;
  std::vector<Command> commands;
};

KernelLogMonitorData LaunchKernelLogMonitor(const CuttlefishConfig& config,
                                            unsigned int number_of_event_pipes);
std::vector<Command> LaunchAdbConnectorIfEnabled(
    const CuttlefishConfig& config);
std::vector<Command> LaunchSocketVsockProxyIfEnabled(
    const CuttlefishConfig& config, SharedFD adbd_events_pipe);
std::vector<Command> LaunchModemSimulatorIfEnabled(
    const CuttlefishConfig& config);

std::vector<Command> LaunchVNCServer(const CuttlefishConfig& config);

std::vector<Command> LaunchTombstoneReceiver(const CuttlefishConfig& config);
std::vector<Command> LaunchRootCanal(const CuttlefishConfig& config);
std::vector<Command> LaunchLogcatReceiver(const CuttlefishConfig& config);
std::vector<Command> LaunchConfigServer(const CuttlefishConfig& config);

std::vector<Command> LaunchWebRTC(const CuttlefishConfig& config,
                                  SharedFD kernel_log_events_pipe);

std::vector<Command> LaunchMetrics();

std::vector<Command> LaunchGnssGrpcProxyServerIfEnabled(
    const CuttlefishConfig& config);

std::vector<Command> LaunchSecureEnvironment(const CuttlefishConfig& config);

std::vector<Command> LaunchBluetoothConnector(const CuttlefishConfig& config);
std::vector<Command> LaunchVehicleHalServerIfEnabled(
    const CuttlefishConfig& config);

std::vector<Command> LaunchConsoleForwarderIfEnabled(
    const CuttlefishConfig& config);

} // namespace cuttlefish
