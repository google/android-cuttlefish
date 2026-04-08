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

// Component to share data between MediaConfigsFlag and MediaConfigsFragment
class MediaConfigs {
 public:
  virtual ~MediaConfigs() = default;

  virtual std::string Name() const = 0;

  virtual const std::vector<CuttlefishConfig::MediaConfig>& GetConfigs()
      const = 0;
  virtual void SetConfigs(
      const std::vector<CuttlefishConfig::MediaConfig>& configs) = 0;
};

// Component to parse the --media command line flag and update the
// MediaConfigs.
class MediaConfigsFlag : public FlagFeature {};

// Component to serialize and deserialize the MediaConfigs to/from Json.
class MediaConfigsFragment : public ConfigFragment {};

fruit::Component<MediaConfigs> MediaConfigsComponent();

fruit::Component<fruit::Required<MediaConfigs, ConfigFlag>,
                 MediaConfigsFlag>
MediaConfigsFlagComponent();

fruit::Component<fruit::Required<MediaConfigs>, MediaConfigsFragment>
MediaConfigsFragmentComponent();

}  // namespace cuttlefish
