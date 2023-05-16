/*
 * Copyright (C) 2023 The Android Open Source Project
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

#include <fruit/fruit.h>

#include "host/commands/assemble_cvd/boot_config.h"
#include "host/libs/config/cuttlefish_config.h"
#include "host/libs/config/feature.h"

namespace cuttlefish {

class KernelRamdiskRepacker : public SetupFeature {};

fruit::Component<fruit::Required<const CuttlefishConfig,
                                 const CuttlefishConfig::InstanceSpecific>,
                 KernelRamdiskRepacker>
KernelRamdiskRepackerComponent();

class GeneratePersistentBootconfig : public SetupFeature {};

fruit::Component<fruit::Required<const CuttlefishConfig,
                                 const CuttlefishConfig::InstanceSpecific>,
                 GeneratePersistentBootconfig>
GeneratePersistentBootconfigComponent();

fruit::Component<fruit::Required<const CuttlefishConfig, KernelRamdiskRepacker>>
Gem5ImageUnpackerComponent();

class GeneratePersistentVbmeta : public SetupFeature {};

fruit::Component<
    fruit::Required<const CuttlefishConfig::InstanceSpecific,
                    InitBootloaderEnvPartition, GeneratePersistentBootconfig>,
    GeneratePersistentVbmeta>
GeneratePersistentVbmetaComponent();

}  // namespace cuttlefish
