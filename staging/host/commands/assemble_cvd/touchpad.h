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

// Component to share data between TouchpadConfigsFlag and
// TouchpadConfigsFragment.
class TouchpadsConfigs {
 public:
  virtual ~TouchpadsConfigs() = default;

  virtual std::string Name() const = 0;

  virtual const std::vector<CuttlefishConfig::TouchpadConfig>& GetConfigs()
      const = 0;
  virtual void SetConfigs(
      const std::vector<CuttlefishConfig::TouchpadConfig>& configs) = 0;
};

// Component to parse the --touchpad command line flag and update the
// TouchpadConfigs.
class TouchpadsConfigsFlag : public FlagFeature {};

fruit::Component<TouchpadsConfigs> TouchpadsConfigsComponent();

fruit::Component<fruit::Required<TouchpadsConfigs, ConfigFlag>,
                 TouchpadsConfigsFlag>
TouchpadsConfigsFlagComponent();

}  // namespace cuttlefish
