/*
 * Copyright (C) 2023 The Android Open Source Project
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

#include "host/commands/assemble_cvd/disk/disk.h"

#include <string>

#include "common/libs/utils/files.h"
#include "common/libs/utils/result.h"
#include "host/libs/config/data_image.h"

namespace cuttlefish {

Result<void> InitializeFactoryResetProtected(
    const CuttlefishConfig::InstanceSpecific& instance) {
  if (instance.protected_vm()) {
    return {};
  }
  auto frp = instance.factory_reset_protected_path();
  if (FileExists(frp)) {
    return {};
  }
  CF_EXPECT(CreateBlankImage(frp, 1 /* mb */, "none"),
            "Failed to create \"" << frp << "\"");
  return {};
}

}  // namespace cuttlefish
