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

#include "cuttlefish/host/commands/cvd/fetch/vector_flags.h"

#include <stdlib.h>

#include <optional>
#include <vector>

#include "cuttlefish/common/libs/utils/flag_parser.h"
#include "cuttlefish/host/libs/web/android_build_string.h"
#include "cuttlefish/result/result.h"

namespace cuttlefish {

std::vector<Flag> VectorFlags::Flags() {
  std::vector<Flag> flags;

  flags.emplace_back(
      GflagsCompatFlag("target_subdirectory", this->target_subdirectory)
          .Help("Target subdirectory to fetch files into.  Specifically aimed "
                "at organizing builds when there are multiple fetches. "
                "**Note**: directory separator automatically prepended, only "
                "give the subdirectory name."));
  flags.emplace_back(
      GflagsCompatFlag("default_build", this->default_build)
          .Help("source for the cuttlefish build to use (vendor.img + host)"));
  flags.emplace_back(GflagsCompatFlag("system_build", this->system_build)
                         .Help("source for system.img and product.img"));
  flags.emplace_back(GflagsCompatFlag("kernel_build", this->kernel_build)
                         .Help("source for the kernel or gki target"));
  flags.emplace_back(GflagsCompatFlag("boot_build", this->boot_build)
                         .Help("source for the boot or gki target"));
  flags.emplace_back(
      GflagsCompatFlag("bootloader_build", this->bootloader_build)
          .Help("source for the bootloader target"));
  flags.emplace_back(GflagsCompatFlag("android_efi_loader_build",
                                      this->android_efi_loader_build)
                         .Help("source for the uefi app target"));
  flags.emplace_back(GflagsCompatFlag("otatools_build", this->otatools_build)
                         .Help("source for the host ota tools"));
  flags.emplace_back(
      GflagsCompatFlag("test_suites_build", this->test_suites_build)
          .Help("source for the test suites build"));
  flags.emplace_back(
      GflagsCompatFlag("chrome_os_build", this->chrome_os_build)
          .Help("source for a ChromeOS build. Formatted as as a numeric build "
                "id, or '<project>/<bucket>/<builder>'"));

  flags.emplace_back(GflagsCompatFlag("boot_artifact", this->boot_artifact)
                         .Help("name of the boot image in boot_build"));
  flags.emplace_back(GflagsCompatFlag("download_img_zip",
                                      this->download_img_zip,
                                      kDefaultDownloadImgZip)
                         .Help("Whether to fetch the -img-*.zip file."));
  flags.emplace_back(
      GflagsCompatFlag("download_target_files_zip",
                       this->download_target_files_zip,
                       kDefaultDownloadTargetFilesZip)
          .Help("Whether to fetch the -target_files-*.zip file."));
  flags.emplace_back(
      GflagsCompatFlag("dynamic_super_image", this->dynamic_super_image,
                       kDefaultDynamicSuperImageFragments)
          .Help("Fetch the super image members as independent files."));

  return flags;
}

Result<int> VectorFlags::NumberOfBuilds() const {
  std::optional<size_t> number_of_builds;
  for (const auto& flag_size :
       {this->default_build.size(), this->system_build.size(),
        this->kernel_build.size(), this->boot_build.size(),
        this->bootloader_build.size(), this->android_efi_loader_build.size(),
        this->otatools_build.size(), this->test_suites_build.size(),
        this->chrome_os_build.size(),
        this->boot_artifact.size(), this->download_img_zip.size(),
        this->download_target_files_zip.size(),
        this->target_subdirectory.size()}) {
    if (flag_size == 0) {
      // a size zero flag vector means the flag was not given
      continue;
    }
    if (number_of_builds) {
      CF_EXPECT(
          flag_size == *number_of_builds,
          "Mismatched flag lengths: " << *number_of_builds << "," << flag_size);
    }
    number_of_builds = flag_size;
  }
  // if no flags had values there is 1 all-default build
  return number_of_builds.value_or(1);
}

}  // namespace cuttlefish
