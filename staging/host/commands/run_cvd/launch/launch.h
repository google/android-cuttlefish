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

#include "common/libs/fs/shared_fd.h"
#include "common/libs/utils/subprocess.h"
#include "host/commands/run_cvd/launch/auto_cmd.h"
#include "host/commands/run_cvd/launch/grpc_socket_creator.h"
#include "host/commands/run_cvd/launch/log_tee_creator.h"
#include "host/commands/run_cvd/launch/snapshot_control_files.h"
#include "host/commands/run_cvd/launch/webrtc_recorder.h"
#include "host/commands/run_cvd/launch/wmediumd_server.h"
#include "host/libs/config/command_source.h"
#include "host/libs/config/custom_actions.h"
#include "host/libs/config/cuttlefish_config.h"
#include "host/libs/config/feature.h"
#include "host/libs/config/kernel_log_pipe_provider.h"
#include "host/libs/vm_manager/vm_manager.h"

namespace cuttlefish {

Result<std::optional<MonitorCommand>> UwbConnector(
    const CuttlefishConfig&, const CuttlefishConfig::InstanceSpecific&);

std::optional<MonitorCommand> AutomotiveProxyService(const CuttlefishConfig&);

fruit::Component<fruit::Required<const CuttlefishConfig, LogTeeCreator,
                                 const CuttlefishConfig::InstanceSpecific>>
VhostDeviceVsockComponent();

Result<std::optional<MonitorCommand>> BluetoothConnector(
    const CuttlefishConfig&, const CuttlefishConfig::InstanceSpecific&);

Result<MonitorCommand> NfcConnector(const CuttlefishConfig&,
                                    const CuttlefishConfig::InstanceSpecific&);

fruit::Component<fruit::Required<const CuttlefishConfig::InstanceSpecific>,
                 KernelLogPipeProvider>
KernelLogMonitorComponent();

Result<MonitorCommand> LogcatReceiver(
    const CuttlefishConfig::InstanceSpecific&);
std::string LogcatInfo(const CuttlefishConfig::InstanceSpecific&);

Result<std::optional<MonitorCommand>> CasimirControlServer(
    const CuttlefishConfig&, const CuttlefishConfig::InstanceSpecific&,
    GrpcSocketCreator&);

Result<std::optional<MonitorCommand>> ConsoleForwarder(
    const CuttlefishConfig::InstanceSpecific&);
std::string ConsoleInfo(const CuttlefishConfig::InstanceSpecific&);

fruit::Component<fruit::Required<const CuttlefishConfig::InstanceSpecific,
                                 GrpcSocketCreator>>
ControlEnvProxyServerComponent();

Result<std::optional<MonitorCommand>> GnssGrpcProxyServer(
    const CuttlefishConfig::InstanceSpecific&, GrpcSocketCreator&);

std::optional<MonitorCommand> MetricsService(const CuttlefishConfig&);

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

Result<std::vector<MonitorCommand>> Casimir(
    const CuttlefishConfig&, const CuttlefishConfig::InstanceSpecific&,
    LogTeeCreator&);

Result<std::vector<MonitorCommand>> Pica(
    const CuttlefishConfig&, const CuttlefishConfig::InstanceSpecific&,
    LogTeeCreator&);

MonitorCommand EchoServer(GrpcSocketCreator& grpc_socket);

fruit::Component<fruit::Required<const CuttlefishConfig,
                                 const CuttlefishConfig::InstanceSpecific>>
NetsimServerComponent();

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

Result<std::optional<MonitorCommand>> ModemSimulator(
    const CuttlefishConfig::InstanceSpecific&);

fruit::Component<
    fruit::Required<const CuttlefishConfig, KernelLogPipeProvider,
                    const CuttlefishConfig::InstanceSpecific,
                    const CustomActionConfigProvider, WebRtcRecorder>>
launchStreamerComponent();

fruit::Component<WebRtcRecorder> WebRtcRecorderComponent();

fruit::Component<
    fruit::Required<const CuttlefishConfig,
                    const CuttlefishConfig::InstanceSpecific, LogTeeCreator>>
McuComponent();

std::optional<MonitorCommand> VhalProxyServer(
    const CuttlefishConfig&, const CuttlefishConfig::InstanceSpecific&);
}  // namespace cuttlefish
