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
#include "cuttlefish/host/libs/config/config_instance_derived.h"

#include <string>

#include "cuttlefish/common/libs/utils/files.h"
#include "cuttlefish/host/libs/config/cuttlefish_config.h"

namespace cuttlefish {

std::string LogcatPipeName(const CuttlefishConfig::InstanceSpecific& ins) {
  return AbsolutePath(ins.PerInstanceInternalPath("logcat-pipe"));
}

}  // namespace cuttlefish

