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

#include "cuttlefish/host/libs/web/android_build.h"
#include "cuttlefish/host/libs/web/chrome_os_build_string.h"

namespace cuttlefish {

struct Builds {
  std::optional<Build> default_build;
  std::optional<Build> system;
  std::optional<Build> kernel;
  std::optional<Build> boot;
  std::optional<Build> bootloader;
  std::optional<Build> android_efi_loader;
  std::optional<Build> otatools;
  std::optional<Build> test_suites;
  std::optional<ChromeOsBuildString> chrome_os;
};

}  // namespace cuttlefish
