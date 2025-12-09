//
// Copyright (C) 2023 The Android Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#pragma once

#include <optional>
#include <string>
#include <vector>

#include "cuttlefish/common/libs/utils/result.h"
#include "cuttlefish/host/libs/web/android_build_string.h"
#include "cuttlefish/host/libs/web/chrome_os_build_string.h"

namespace cuttlefish {

inline constexpr bool kDefaultDownloadImgZip = true;
inline constexpr bool kDefaultDownloadTargetFilesZip = false;
// TODO: schuffelen - Enable this by default.
inline constexpr bool kDefaultDynamicSuperImageFragments = false;

struct VectorFlags {
  std::vector<Flag> Flags();
  Result<int> NumberOfBuilds() const;

  std::vector<std::string> target_subdirectory;
  std::vector<std::optional<BuildString>> default_build;
  std::vector<std::optional<BuildString>> system_build;
  std::vector<std::optional<BuildString>> kernel_build;
  std::vector<std::optional<BuildString>> boot_build;
  std::vector<std::optional<BuildString>> bootloader_build;
  std::vector<std::optional<BuildString>> android_efi_loader_build;
  std::vector<std::optional<BuildString>> otatools_build;
  std::vector<std::optional<BuildString>> test_suites_build;
  std::vector<std::optional<ChromeOsBuildString>> chrome_os_build;
  std::vector<bool> download_img_zip;
  std::vector<bool> download_target_files_zip;
  std::vector<std::string> boot_artifact;
  std::vector<bool> dynamic_super_image;
};

}  // namespace cuttlefish
