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

#include <vector>

#include "cuttlefish/common/libs/utils/result.h"
#include "cuttlefish/host/commands/assemble_cvd/guest_config.h"
#include "cuttlefish/host/libs/config/cuttlefish_config.h"

namespace cuttlefish {

class VmManagerFlag {
 public:
  static Result<VmManagerFlag> FromGlobalGflags(
      const std::vector<GuestConfig>&);

  VmmMode Mode() const;

 private:
  explicit VmManagerFlag(VmmMode);

  VmmMode mode_;
};

}  // namespace cuttlefish
