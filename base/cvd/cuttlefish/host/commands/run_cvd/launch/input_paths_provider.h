//
// Copyright (C) 2024 The Android Open Source Project
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

#include <vector>

#include "cuttlefish/host/commands/run_cvd/launch/log_tee_creator.h"
#include "cuttlefish/host/libs/config/cuttlefish_config.h"
#include "cuttlefish/host/libs/feature/feature.h"

namespace cuttlefish {

// Feature that provides access to the connections to the input devices.
// Such connections are file descriptors over which (virtio_) input events can
// be written to inject them to the VM and (virtio_) status updates can be read.
class InputPathsProvider : public virtual SetupFeature {
 public:
  virtual ~InputPathsProvider() = default;

  virtual std::string RotaryDevicePath() const = 0;
  virtual std::string MousePath() const = 0;
  virtual std::string GamepadPath() const = 0;
  virtual std::string KeyboardPath() const = 0;
  virtual std::string SwitchesPath() const = 0;
  virtual std::vector<std::string> TouchscreenPaths() const = 0;
  virtual std::vector<std::string> TouchpadPaths() const = 0;
};

fruit::Component<fruit::Required<const CuttlefishConfig::InstanceSpecific>,
                 InputPathsProvider, LogTeeCreator>
VhostInputDevicesComponent();

}  // namespace cuttlefish
