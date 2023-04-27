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

#include <fruit/fruit.h>

#include "common/libs/fs/shared_fd.h"
#include "common/libs/utils/subprocess.h"
#include "host/commands/run_cvd/launch/grpc_socket_creator.h"
#include "host/commands/run_cvd/launch/log_tee_creator.h"
#include "host/libs/config/command_source.h"
#include "host/libs/config/custom_actions.h"
#include "host/libs/config/cuttlefish_config.h"
#include "host/libs/config/feature.h"
#include "host/libs/config/kernel_log_pipe_provider.h"
#include "host/libs/vm_manager/vm_manager.h"

namespace cuttlefish {

fruit::Component<fruit::Required<const CuttlefishConfig,
                                 const CuttlefishConfig::InstanceSpecific>>
UwbConnectorComponent();

fruit::Component<fruit::Required<const CuttlefishConfig,
                                 const CuttlefishConfig::InstanceSpecific>>
BluetoothConnectorComponent();

fruit::Component<fruit::Required<const CuttlefishConfig::InstanceSpecific>,
                 KernelLogPipeProvider>
KernelLogMonitorComponent();

fruit::Component<fruit::Required<const CuttlefishConfig::InstanceSpecific>>
LogcatReceiverComponent();

fruit::Component<fruit::Required<const CuttlefishConfig::InstanceSpecific>>
ConfigServerComponent();

fruit::Component<fruit::Required<const CuttlefishConfig::InstanceSpecific>>
ConsoleForwarderComponent();

fruit::Component<fruit::Required<const CuttlefishConfig::InstanceSpecific,
                                 GrpcSocketCreator>>
GnssGrpcProxyServerComponent();

fruit::Component<fruit::Required<const CuttlefishConfig>>
MetricsServiceComponent();

fruit::Component<
    fruit::Required<const CuttlefishConfig,
                    const CuttlefishConfig::InstanceSpecific, LogTeeCreator>>
OpenWrtComponent();

fruit::Component<fruit::Required<const CuttlefishConfig::InstanceSpecific,
                                 GrpcSocketCreator>>
OpenwrtControlServerComponent();

fruit::Component<
    fruit::Required<const CuttlefishConfig,
                    const CuttlefishConfig::InstanceSpecific, LogTeeCreator>>
RootCanalComponent();

fruit::Component<
    fruit::Required<const CuttlefishConfig,
                    const CuttlefishConfig::InstanceSpecific, LogTeeCreator>>
PicaComponent();

fruit::Component<fruit::Required<GrpcSocketCreator>> EchoServerComponent();

fruit::Component<fruit::Required<const CuttlefishConfig,
                                 const CuttlefishConfig::InstanceSpecific>>
NetsimServerComponent();

fruit::Component<fruit::Required<const CuttlefishConfig,
                                 const CuttlefishConfig::InstanceSpecific,
                                 KernelLogPipeProvider>>
SecureEnvComponent();

fruit::Component<fruit::Required<const CuttlefishConfig::InstanceSpecific>>
TombstoneReceiverComponent();

fruit::Component<fruit::Required<const CuttlefishConfig,
                                 const CuttlefishConfig::InstanceSpecific,
                                 LogTeeCreator, GrpcSocketCreator>>
WmediumdServerComponent();

fruit::Component<fruit::Required<const CuttlefishConfig,
                                 const CuttlefishConfig::InstanceSpecific>>
launchModemComponent();

fruit::Component<fruit::Required<const CuttlefishConfig, KernelLogPipeProvider,
                                 const CuttlefishConfig::InstanceSpecific,
                                 const CustomActionConfigProvider>>
launchStreamerComponent();

}  // namespace cuttlefish
