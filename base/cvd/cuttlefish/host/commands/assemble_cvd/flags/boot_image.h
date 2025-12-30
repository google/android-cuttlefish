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

#include "cuttlefish/host/commands/assemble_cvd/flags/flag_base.h"
#include "cuttlefish/host/commands/assemble_cvd/flags/system_image_dir.h"

namespace cuttlefish {

/* Android boot image path flag, --boot_image */
class BootImageFlag : public FlagBase<std::string> {
 public:
  static BootImageFlag FromGlobalGflags(const SystemImageDirFlag&);

  bool IsDefault() const;

 private:
  BootImageFlag(std::vector<std::string>, bool is_default);

  bool is_default_;
};

}  // namespace cuttlefish
