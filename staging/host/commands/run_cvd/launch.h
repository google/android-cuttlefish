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
#include "host/commands/run_cvd/process_monitor.h"
#include "host/libs/config/cuttlefish_config.h"

namespace cuttlefish {

std::vector<SharedFD> LaunchKernelLogMonitor(
    const CuttlefishConfig& config,
    ProcessMonitor* process_monitor,
    unsigned int number_of_event_pipes);
void LaunchAdbConnectorIfEnabled(ProcessMonitor* process_monitor,
                                 const CuttlefishConfig& config);
void LaunchSocketVsockProxyIfEnabled(ProcessMonitor* process_monitor,
                                     const CuttlefishConfig& config,
                                     SharedFD adbd_events_pipe);
void LaunchModemSimulatorIfEnabled(const CuttlefishConfig& config,
                                   ProcessMonitor* process_monitor);

void LaunchVNCServer(const CuttlefishConfig& config,
                     ProcessMonitor* process_monitor);

void LaunchTombstoneReceiver(const CuttlefishConfig& config,
                             ProcessMonitor* process_monitor);
void LaunchRootCanal(const CuttlefishConfig& config,
                     ProcessMonitor* process_monitor);
void LaunchLogcatReceiver(const CuttlefishConfig& config,
                          ProcessMonitor* process_monitor);
void LaunchConfigServer(const CuttlefishConfig& config,
                        ProcessMonitor* process_monitor);

void LaunchWebRTC(ProcessMonitor* process_monitor,
                  const CuttlefishConfig& config,
                  SharedFD kernel_log_events_pipe);

void LaunchMetrics(ProcessMonitor* process_monitor);

void LaunchGnssGrpcProxyServerIfEnabled(const CuttlefishConfig& config,
                                        ProcessMonitor* process_monitor);

void LaunchSecureEnvironment(ProcessMonitor* process_monitor,
                             const CuttlefishConfig& config);

void LaunchBluetoothConnector(ProcessMonitor* process_monitor,
                              const CuttlefishConfig& config);

void LaunchCustomActionServers(Command& webrtc_cmd,
                               ProcessMonitor* process_monitor,
                               const CuttlefishConfig& config);

void LaunchVehicleHalServerIfEnabled(const CuttlefishConfig& config,
                                     ProcessMonitor* process_monitor);

void LaunchConsoleForwarderIfEnabled(const CuttlefishConfig& config,
                                     ProcessMonitor* process_monitor);

} // namespace cuttlefish
