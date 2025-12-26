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

#include "cuttlefish/host/commands/assemble_cvd/disk/misc_image.h"

#include <android-base/logging.h>

#include "cuttlefish/common/libs/utils/files.h"
#include "cuttlefish/host/libs/config/cuttlefish_config.h"
#include "cuttlefish/host/libs/config/data_image.h"
#include "cuttlefish/result/result.h"

namespace cuttlefish {

MiscImage::MiscImage(const CuttlefishConfig::InstanceSpecific& instance) {
  path_ = instance.PerInstancePath(kName);
  ready_ = FileHasContent(path_);
}

std::string MiscImage::Name() const { return std::string(kName); }

Result<std::string> MiscImage::Generate() {
  if (!ready_) {
    CF_EXPECT(CreateBlankImage(path_, 1 /* mb */, "none"),
              "Failed to create misc image");
    ready_ = true;
  }
  return path_;
}

Result<std::string> MiscImage::Path() const {
  CF_EXPECT(!!ready_);
  return path_;
}


}  // namespace cuttlefish
