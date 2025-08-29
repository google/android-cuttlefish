/*
 * Copyright (C) 2025 The Android Open Source Project
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

#include <cstdlib>
#include <vector>

#include "cuttlefish/common/libs/utils/result.h"
#include "cuttlefish/host/commands/assemble_cvd/guest_config.h"

namespace cuttlefish {

class BlankDataImageMbFlag {
 public:
  static Result<BlankDataImageMbFlag> FromGlobalGflags(
      const std::vector<GuestConfig>& guest_configs);

  int ForIndex(std::size_t index) const;

 private:
  BlankDataImageMbFlag(const int, std::vector<int>);

  int default_value_ = 0;
  std::vector<int> blank_data_image_mb_values_;
};

}  // namespace cuttlefish
