//
// Copyright (C) 2025 The Android Open Source Project
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

#include "cuttlefish/host/commands/run_cvd/launch/enable_multitouch.h"

#include "cuttlefish/host/libs/config/cuttlefish_config.h"
#include "cuttlefish/host/libs/config/guest_os.h"

namespace cuttlefish {

bool ShouldEnableMultitouch(
    const CuttlefishConfig::InstanceSpecific& instance) {
  return GuestOsFromBootFlow(instance.boot_flow()) != GuestOs::ChromeOs;
}

}  // namespace cuttlefish

