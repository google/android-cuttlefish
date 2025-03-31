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

#include <fruit/fruit.h>

#include "cuttlefish/host/commands/run_cvd/launch/input_connections_provider.h"
#include "cuttlefish/host/commands/run_cvd/launch/log_tee_creator.h"
#include "cuttlefish/host/commands/run_cvd/launch/webrtc_controller.h"
#include "cuttlefish/host/libs/config/command_source.h"
#include "cuttlefish/host/libs/config/custom_actions.h"
#include "cuttlefish/host/libs/config/cuttlefish_config.h"
#include "cuttlefish/host/libs/config/kernel_log_pipe_provider.h"

namespace cuttlefish {

fruit::Component<fruit::Required<
    const CuttlefishConfig, KernelLogPipeProvider, InputConnectionsProvider,
    const CuttlefishConfig::InstanceSpecific, const CustomActionConfigProvider,
    WebRtcController>>
launchStreamerComponent();

fruit::Component<WebRtcController> WebRtcControllerComponent();

fruit::Component<fruit::Required<const CuttlefishConfig::InstanceSpecific>,
                 InputConnectionsProvider, LogTeeCreator>
VhostInputDevicesComponent();

}  // namespace cuttlefish
