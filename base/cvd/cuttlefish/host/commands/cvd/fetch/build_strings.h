//
// Copyright (C) 2019 The Android Open Source Project
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

#include "cuttlefish/host/commands/cvd/fetch/fetch_cvd_parser.h"
#include "cuttlefish/host/libs/web/android_build_string.h"
#include "cuttlefish/host/libs/web/chrome_os_build_string.h"

namespace cuttlefish {

struct BuildStrings {
  static BuildStrings Create(const VectorFlags& flags, int index);

  std::optional<BuildString> default_build;
  std::optional<BuildString> system_build;
  std::optional<BuildString> kernel_build;
  std::optional<BuildString> boot_build;
  std::optional<BuildString> bootloader_build;
  std::optional<BuildString> android_efi_loader_build;
  std::optional<BuildString> otatools_build;
  std::optional<BuildString> test_suites_build;
  std::optional<BuildString> host_package_build;
  std::optional<ChromeOsBuildString> chrome_os_build;
};

}  // namespace cuttlefish
