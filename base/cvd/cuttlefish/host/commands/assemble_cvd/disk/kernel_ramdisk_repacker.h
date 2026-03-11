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
#include <string>

#include "cuttlefish/host/commands/assemble_cvd/disk/image_file.h"
#include "cuttlefish/host/commands/assemble_cvd/flags/boot_image.h"
#include "cuttlefish/host/libs/config/cuttlefish_config.h"
#include "cuttlefish/result/result_type.h"

namespace cuttlefish {

class InstanceBootImage : public ImageFile {
 public:
  InstanceBootImage(const CuttlefishConfig&,
                    const CuttlefishConfig::InstanceSpecific&,
                    const BootImageFlag&);

  std::string Name() const override;
  Result<std::string> Generate() override;
  Result<std::string> Path() const override;

 private:
  const CuttlefishConfig* config_;
  const CuttlefishConfig::InstanceSpecific* instance_;
  const BootImageFlag* boot_image_flag_;
  std::optional<std::string> path_;
};

Result<void> RepackKernelRamdisk(
    const CuttlefishConfig& config,
    const CuttlefishConfig::InstanceSpecific& instance);

}  // namespace cuttlefish
