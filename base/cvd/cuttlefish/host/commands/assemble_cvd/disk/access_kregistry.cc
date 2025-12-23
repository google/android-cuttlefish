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

#include "cuttlefish/host/commands/assemble_cvd/disk/access_kregistry.h"

#include "cuttlefish/common/libs/utils/files.h"
#include "cuttlefish/host/libs/config/cuttlefish_config.h"
#include "cuttlefish/host/libs/config/data_image.h"
#include "cuttlefish/result/result.h"

namespace cuttlefish {

Result<void> InitializeAccessKregistryImage(
    const CuttlefishConfig::InstanceSpecific& instance) {
  auto access_kregistry = instance.access_kregistry_path();
  if (FileExists(access_kregistry)) {
    return {};
  }
  CF_EXPECT(CreateBlankImage(access_kregistry, 2 /* mb */, "none"),
            "Failed to create \"" << access_kregistry << "\"");
  return {};
}

}  // namespace cuttlefish
