/*
 * Copyright (C) 2017 The Android Open Source Project
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

#include "host/commands/run_cvd/server_loop.h"

#include <fruit/fruit.h>

#include "host/commands/run_cvd/server_loop_impl.h"
#include "host/libs/config/cuttlefish_config.h"
#include "host/libs/config/feature.h"
#include "host/libs/config/inject.h"

namespace cuttlefish {

ServerLoop::~ServerLoop() = default;

fruit::Component<fruit::Required<const CuttlefishConfig,
                                 const CuttlefishConfig::InstanceSpecific>,
                 ServerLoop>
serverLoopComponent() {
  using run_cvd_impl::ServerLoopImpl;
  return fruit::createComponent()
      .bind<ServerLoop, ServerLoopImpl>()
      .addMultibinding<LateInjected, ServerLoopImpl>()
      .addMultibinding<SetupFeature, ServerLoopImpl>();
}

}  // namespace cuttlefish
