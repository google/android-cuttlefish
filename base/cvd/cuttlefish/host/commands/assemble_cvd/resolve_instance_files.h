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

#include "cuttlefish/host/commands/assemble_cvd/flags/boot_image.h"
#include "cuttlefish/host/commands/assemble_cvd/flags/initramfs_path.h"
#include "cuttlefish/host/commands/assemble_cvd/flags/kernel_path.h"
#include "cuttlefish/host/commands/assemble_cvd/flags/super_image.h"
#include "cuttlefish/host/commands/assemble_cvd/flags/system_image_dir.h"
#include "cuttlefish/host/commands/assemble_cvd/flags/vendor_boot_image.h"
#include "cuttlefish/result/result.h"

namespace cuttlefish {

Result<void> ResolveInstanceFiles(const BootImageFlag&,
                                  const InitramfsPathFlag&,
                                  const KernelPathFlag& kernel_path,
                                  const SuperImageFlag&,
                                  const SystemImageDirFlag& system_image_dir,
                                  const VendorBootImageFlag&);

}  // namespace cuttlefish
