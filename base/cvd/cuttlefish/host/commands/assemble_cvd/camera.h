//
// Copyright (C) 2026 The Android Open Source Project
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

#include "cuttlefish/host/libs/config/config_flag.h"
#include "cuttlefish/host/libs/config/config_fragment.h"
#include "cuttlefish/host/libs/config/cuttlefish_config.h"

namespace cuttlefish {

// Component to share data between CamerasConfigsFlag and CamerasConfigsFragment
class CamerasConfigs {
 public:
  virtual ~CamerasConfigs() = default;

  virtual std::string Name() const = 0;

  virtual const std::vector<CuttlefishConfig::CameraConfig>& GetConfigs()
      const = 0;
  virtual void SetConfigs(
      const std::vector<CuttlefishConfig::CameraConfig>& configs) = 0;
};

// Component to parse the --camera command line flag and update the
// CamerasConfigs.
class CamerasConfigsFlag : public FlagFeature {};

// Component to serialize and deserialize the CamerasConfigs to/from Json.
class CamerasConfigsFragment : public ConfigFragment {};

fruit::Component<CamerasConfigs> CamerasConfigsComponent();

fruit::Component<fruit::Required<CamerasConfigs, ConfigFlag>,
                 CamerasConfigsFlag>
CamerasConfigsFlagComponent();

fruit::Component<fruit::Required<CamerasConfigs>, CamerasConfigsFragment>
CamerasConfigsFragmentComponent();

}  // namespace cuttlefish
