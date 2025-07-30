/*
 * Copyright (C) 2018 The Android Open Source Project
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

#include "cuttlefish/host/libs/config/vmm_mode.h"

#include <ostream>
#include <sstream>
#include <string>
#include <string_view>

#include <android-base/strings.h>

#include "cuttlefish/common/libs/utils/result.h"

namespace cuttlefish {

std::string ToString(VmmMode mode) {
  std::stringstream ss;
  ss << mode;
  return ss.str();
}

std::ostream& operator<<(std::ostream& out, VmmMode vmm) {
  switch (vmm) {
    case VmmMode::kUnknown:
      return out << "unknown";
    case VmmMode::kCrosvm:
      return out << "crosvm";
    case VmmMode::kGem5:
      return out << "gem5";
    case VmmMode::kQemu:
      return out << "qemu_cli";
  }
}

Result<VmmMode> ParseVmm(std::string_view str) {
  if (android::base::EqualsIgnoreCase(str, "crosvm")) {
    return VmmMode::kCrosvm;
  } else if (android::base::EqualsIgnoreCase(str, "gem5")) {
    return VmmMode::kGem5;
  } else if (android::base::EqualsIgnoreCase(str, "qemu_cli")) {
    return VmmMode::kQemu;
  } else {
    return CF_ERRF("\"{}\" is not a valid Vmm.", str);
  }
}

}  // namespace cuttlefish
