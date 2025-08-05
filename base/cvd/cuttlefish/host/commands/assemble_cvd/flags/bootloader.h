/*
 * Copyright (C) 2019 The Android Open Source Project
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

#include <stddef.h>

#include <string>
#include <vector>

#include "cuttlefish/common/libs/utils/result.h"
#include "cuttlefish/host/commands/assemble_cvd/flags/system_image_dir.h"
#include "cuttlefish/host/commands/assemble_cvd/flags/vm_manager.h"
#include "cuttlefish/host/commands/assemble_cvd/guest_config.h"

namespace cuttlefish {

/* Device bootloader flag, `--bootloader` */
class BootloaderFlag {
 public:
  static Result<BootloaderFlag> FromGlobalGflags(
      const std::vector<GuestConfig>&, const SystemImageDirFlag&,
      const VmManagerFlag&);

  std::string BootloaderForInstance(size_t instance_index) const;

 private:
  BootloaderFlag(std::vector<std::string> bootloaders);

  std::vector<std::string> bootloaders_;
};

}  // namespace cuttlefish
