/*
 * Copyright (C) 2025 The Android Open Source Project
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

#include "cuttlefish/host/commands/assemble_cvd/disk/pstore.h"

#include "cuttlefish/common/libs/utils/files.h"
#include "cuttlefish/common/libs/utils/result.h"
#include "cuttlefish/host/libs/config/cuttlefish_config.h"
#include "cuttlefish/host/libs/config/data_image.h"

namespace cuttlefish {

Result<void> InitializePstore(
    const CuttlefishConfig::InstanceSpecific& instance) {
  if (FileExists(instance.pstore_path())) {
    return {};
  }

  CF_EXPECT(CreateBlankImage(instance.pstore_path(), 2 /* mb */, "none"),
            "Failed to create \"" << instance.pstore_path() << "\"");
  return {};
}

}  // namespace cuttlefish
