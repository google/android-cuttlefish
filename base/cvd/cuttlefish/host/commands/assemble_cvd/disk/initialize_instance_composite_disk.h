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

#pragma once

#include <optional>

#include "cuttlefish/common/libs/utils/result.h"
#include "cuttlefish/host/commands/assemble_cvd/disk/factory_reset_protected.h"
#include "cuttlefish/host/commands/assemble_cvd/disk/generate_persistent_bootconfig.h"
#include "cuttlefish/host/commands/assemble_cvd/disk/generate_persistent_vbmeta.h"
#include "cuttlefish/host/libs/config/cuttlefish_config.h"

namespace cuttlefish {

class InstanceCompositeDisk {
 public:
  static Result<InstanceCompositeDisk> Create(
      const CuttlefishConfig&, const CuttlefishConfig::InstanceSpecific&,
      AutoSetup<FactoryResetProtectedImage::Create>::Type&,
      AutoSetup<BootConfigPartition::CreateIfNeeded>::Type&,
      AutoSetup<PersistentVbmeta::Create>::Type&);

 private:
  InstanceCompositeDisk() = default;
};

class ApCompositeDisk {
 public:
  static Result<std::optional<ApCompositeDisk>> Create(
      const CuttlefishConfig&, const CuttlefishConfig::InstanceSpecific&,
      AutoSetup<ApPersistentVbmeta::Create>::Type&);

 private:
  ApCompositeDisk() = default;
};

}  // namespace cuttlefish
