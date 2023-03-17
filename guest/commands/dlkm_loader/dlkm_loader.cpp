/*
 * Copyright (C) 2019 The Android Open Source Project
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

#include <android-base/logging.h>
#include <modprobe/modprobe.h>
#include "android-base/properties.h"

int main(int, char **argv) {
  android::base::InitLogging(argv, android::base::KernelLogger);
  LOG(INFO) << "dlkm loader successfully initialized";
  Modprobe m({"/vendor/lib/modules"}, "modules.load");
  // We should continue loading kernel modules even if some modules fail to
  // load. If we abort loading early, the unloaded modules can cause more
  // problems, making debugging hard.
  // e.g. , bluetooth module break, but we
  // might also see graphics problems, because graphics module gets loaded
  // after bluetooth, and we aborted loading early.
  CHECK(m.LoadListedModules(false))
      << "modules from vendor dlkm weren't loaded correctly";
  LOG(INFO) << "module load count is " << m.GetModuleCount();

  android::base::SetProperty("vendor.dlkm.modules.ready", "true");
  return 0;
}
