//
// Copyright (C) 2023 The Android Open Source Project
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

#include "host/libs/config/config_flag.h"
#include "host/libs/config/config_fragment.h"
#include "host/libs/config/cuttlefish_config.h"
#include "host/libs/config/feature.h"

namespace cuttlefish {

// Component to share data between DisplayConfigsFlag and
// DisplayConfigsFragment.
class DisplaysConfigs {
 public:
  virtual ~DisplaysConfigs() = default;

  virtual std::string Name() const = 0;

  virtual const std::vector<CuttlefishConfig::DisplayConfig>& GetConfigs()
      const = 0;
  virtual void SetConfigs(
      const std::vector<CuttlefishConfig::DisplayConfig>& configs) = 0;
};

// Component to parse the --display command line flag and update the
// DisplayConfigs.
class DisplaysConfigsFlag : public FlagFeature {};

// Component to serialize and deserialize the DisplayConfigs to/from
// Json.
class DisplaysConfigsFragment : public ConfigFragment {};

fruit::Component<DisplaysConfigs> DisplaysConfigsComponent();

fruit::Component<fruit::Required<DisplaysConfigs, ConfigFlag>,
                 DisplaysConfigsFlag>
DisplaysConfigsFlagComponent();

fruit::Component<fruit::Required<DisplaysConfigs>, DisplaysConfigsFragment>
DisplaysConfigsFragmentComponent();

}  // namespace cuttlefish
