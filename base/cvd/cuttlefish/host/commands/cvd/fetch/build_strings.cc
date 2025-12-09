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

#include "cuttlefish/host/commands/cvd/fetch/build_strings.h"

#include "cuttlefish/host/commands/cvd/fetch/fetch_cvd_parser.h"
#include "cuttlefish/host/commands/cvd/fetch/get_optional.h"
#include "cuttlefish/host/libs/web/android_build_string.h"

namespace cuttlefish {

BuildStrings BuildStrings::Create(const VectorFlags& flags, const int index) {
  BuildStrings build_strings = BuildStrings{
      .default_build = GetOptional(flags.default_build, index),
      .system_build = GetOptional(flags.system_build, index),
      .kernel_build = GetOptional(flags.kernel_build, index),
      .boot_build = GetOptional(flags.boot_build, index),
      .bootloader_build = GetOptional(flags.bootloader_build, index),
      .android_efi_loader_build =
          GetOptional(flags.android_efi_loader_build, index),
      .otatools_build = GetOptional(flags.otatools_build, index),
      .test_suites_build = GetOptional(flags.test_suites_build, index),
      .chrome_os_build = GetOptional(flags.chrome_os_build, index),
  };
  std::string possible_boot_artifact =
      GetOptional(flags.boot_artifact, index).value_or("");
  if (!possible_boot_artifact.empty() && build_strings.boot_build) {
    SetFilepath(*build_strings.boot_build, possible_boot_artifact);
  }
  return build_strings;
}

}  // namespace cuttlefish
