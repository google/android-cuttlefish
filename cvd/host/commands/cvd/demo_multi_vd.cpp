/*
 * Copyright (C) 2022 The Android Open Source Project
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
#include "host/commands/cvd/demo_multi_vd.h"

#include "host/commands/cvd/server_command/serial_launch.h"
#include "host/commands/cvd/server_command/serial_preset.h"

namespace cuttlefish {

fruit::Component<fruit::Required<CommandSequenceExecutor>>
DemoMultiVdComponent() {
  return fruit::createComponent()
      .install(cvdSerialLaunchComponent)
      .install(cvdSerialPresetComponent);
}

}  // namespace cuttlefish
