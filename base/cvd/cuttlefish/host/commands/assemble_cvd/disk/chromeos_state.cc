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

#include "cuttlefish/host/commands/assemble_cvd/disk/chromeos_state.h"

#include <optional>
#include <string>

#include "cuttlefish/common/libs/utils/files.h"
#include "cuttlefish/common/libs/utils/result.h"
#include "cuttlefish/host/libs/config/boot_flow.h"
#include "cuttlefish/host/libs/config/cuttlefish_config.h"
#include "cuttlefish/host/libs/config/data_image.h"

namespace cuttlefish {

static constexpr char kImageName[] = "chromeos_state.img";
static constexpr int kImageSizeMb = 8096;
static constexpr char kFilesystemFormat[] = "ext4";

Result<std::optional<ChromeOsStateImage>> ChromeOsStateImage::CreateIfNecessary(
    const CuttlefishConfig::InstanceSpecific& instance) {
  if (instance.boot_flow() != BootFlow::ChromeOs) {
    return std::nullopt;
  }
  std::string path = AbsolutePath(instance.PerInstancePath(kImageName));
  if (!FileExists(path)) {
    CF_EXPECT(CreateBlankImage(path, kImageSizeMb,kFilesystemFormat));
  }
  return ChromeOsStateImage(std::move(path));
}

ChromeOsStateImage::ChromeOsStateImage(std::string path) : path_(path) {}

const std::string& ChromeOsStateImage::FilePath() const {
  return path_;
}

}  // namespace cuttlefish
