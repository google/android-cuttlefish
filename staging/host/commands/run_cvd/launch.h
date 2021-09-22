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

#include <fruit/fruit.h>

#include <string>
#include <vector>

#include "common/libs/fs/shared_fd.h"
#include "common/libs/utils/subprocess.h"
#include "host/libs/config/command_source.h"
#include "host/libs/config/custom_actions.h"
#include "host/libs/config/cuttlefish_config.h"
#include "host/libs/config/feature.h"
#include "host/libs/config/kernel_log_pipe_provider.h"
#include "host/libs/vm_manager/vm_manager.h"

namespace cuttlefish {

fruit::Component<fruit::Required<const CuttlefishConfig, vm_manager::VmManager,
                                 const CuttlefishConfig::InstanceSpecific>,
                 KernelLogPipeProvider>
launchComponent();

fruit::Component<fruit::Required<const CuttlefishConfig,
                                 const CuttlefishConfig::InstanceSpecific>>
launchModemComponent();

fruit::Component<fruit::Required<const CuttlefishConfig, KernelLogPipeProvider,
                                 const CuttlefishConfig::InstanceSpecific,
                                 const CustomActionConfigProvider>>
launchStreamerComponent();

} // namespace cuttlefish
