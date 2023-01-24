/*
 * Copyright (C) 2018 The Android Open Source Project
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

#include "host/libs/config/config_flag.h"
#include "host/libs/config/config_fragment.h"
#include "host/libs/config/cuttlefish_config.h"
#include "host/libs/config/feature.h"

namespace cuttlefish {

class FastbootConfig {
 public:
  virtual ~FastbootConfig() = default;

  virtual bool ProxyFastboot() const = 0;
  virtual bool SetProxyFastboot(bool) = 0;
};

class FastbootConfigFragment : public ConfigFragment {};
class FastbootConfigFlag : public FlagFeature {};

fruit::Component<FastbootConfig>
FastbootConfigComponent();
fruit::Component<fruit::Required<FastbootConfig, ConfigFlag>, FastbootConfigFlag>
FastbootConfigFlagComponent();
fruit::Component<fruit::Required<FastbootConfig>, FastbootConfigFragment>
FastbootConfigFragmentComponent();
fruit::Component<fruit::Required<const CuttlefishConfig::InstanceSpecific, const FastbootConfig>>
LaunchFastbootComponent();

}  // namespace cuttlefish
