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

#include <optional>
#include <string>
#include <vector>

#include <fruit/fruit.h>

#include "cuttlefish/host/commands/run_cvd/launch/grpc_socket_creator.h"
#include "cuttlefish/host/commands/run_cvd/launch/input_connections_provider.h"
#include "cuttlefish/host/commands/run_cvd/launch/log_tee_creator.h"
#include "cuttlefish/host/commands/run_cvd/launch/sensors_socket_pair.h"
#include "cuttlefish/host/commands/run_cvd/launch/snapshot_control_files.h"
#include "cuttlefish/host/commands/run_cvd/launch/webrtc_controller.h"
#include "cuttlefish/host/commands/run_cvd/launch/wmediumd_server.h"
#include "cuttlefish/host/libs/config/command_source.h"
#include "cuttlefish/host/libs/config/custom_actions.h"
#include "cuttlefish/host/libs/config/cuttlefish_config.h"
#include "cuttlefish/host/libs/config/feature.h"
#include "cuttlefish/host/libs/config/kernel_log_pipe_provider.h"

namespace cuttlefish {

Result<std::optional<MonitorCommand>> UwbConnector(
    const CuttlefishConfig&, const CuttlefishConfig::InstanceSpecific&);

fruit::Component<fruit::Required<const CuttlefishConfig, LogTeeCreator,
                                 const CuttlefishConfig::InstanceSpecific>>
VhostDeviceVsockComponent();

Result<MonitorCommand> NfcConnector(
    const CuttlefishConfig::EnvironmentSpecific&,
    const CuttlefishConfig::InstanceSpecific&);

fruit::Component<fruit::Required<
    const CuttlefishConfig, const CuttlefishConfig::EnvironmentSpecific,
    const CuttlefishConfig::InstanceSpecific, LogTeeCreator, WmediumdServer>>
OpenWrtComponent();

fruit::Component<fruit::Required<const CuttlefishConfig,
                                 const CuttlefishConfig::EnvironmentSpecific,
                                 GrpcSocketCreator>>
OpenwrtControlServerComponent();

fruit::Component<
    fruit::Required<const CuttlefishConfig,
                    const CuttlefishConfig::InstanceSpecific, LogTeeCreator>>
RootCanalComponent();

Result<std::vector<MonitorCommand>> Pica(
    const CuttlefishConfig&, const CuttlefishConfig::InstanceSpecific&,
    LogTeeCreator&);

Result<std::optional<MonitorCommand>> ScreenRecordingServer(GrpcSocketCreator&);

Result<MonitorCommand> SecureEnv(const CuttlefishConfig&,
                                 const CuttlefishConfig::InstanceSpecific&,
                                 AutoSnapshotControlFiles::Type&,
                                 KernelLogPipeProvider&);

Result<MonitorCommand> TombstoneReceiver(
    const CuttlefishConfig::InstanceSpecific&);

fruit::Component<fruit::Required<
    const CuttlefishConfig, const CuttlefishConfig::EnvironmentSpecific,
    const CuttlefishConfig::InstanceSpecific, LogTeeCreator, GrpcSocketCreator>>
WmediumdServerComponent();

fruit::Component<fruit::Required<
    const CuttlefishConfig, KernelLogPipeProvider, InputConnectionsProvider,
    const CuttlefishConfig::InstanceSpecific, const CustomActionConfigProvider,
    WebRtcController>>
launchStreamerComponent();

fruit::Component<WebRtcController> WebRtcControllerComponent();

fruit::Component<fruit::Required<const CuttlefishConfig::InstanceSpecific>,
                 InputConnectionsProvider, LogTeeCreator>
VhostInputDevicesComponent();

std::optional<MonitorCommand> VhalProxyServer(
    const CuttlefishConfig&, const CuttlefishConfig::InstanceSpecific&);

fruit::Component<fruit::Required<const CuttlefishConfig, LogTeeCreator,
                                 const CuttlefishConfig::InstanceSpecific>>
Ti50EmulatorComponent();

Result<MonitorCommand> SensorsSimulator(
    const CuttlefishConfig::InstanceSpecific&,
    AutoSensorsSocketPair::Type& sensors_socket_pair);
}  // namespace cuttlefish
