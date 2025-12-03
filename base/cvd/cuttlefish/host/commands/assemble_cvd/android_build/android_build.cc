//
// Copyright (C) 2025 The Android Open Source Project
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

#include "cuttlefish/host/commands/assemble_cvd/android_build/android_build.h"

#include <functional>
#include <optional>
#include <ostream>
#include <set>
#include <string>
#include <string_view>

#include "cuttlefish/common/libs/utils/result.h"

namespace cuttlefish {

Result<std::set<std::string, std::less<void>>> AndroidBuild::Images() {
  return CF_ERRF("Unimplemented for '{}'", *this);
}

Result<std::string> AndroidBuild::ImageFile(
    std::string_view name, std::optional<std::string_view> extract_dir) {
  return CF_ERRF("Unimplemented for '{}': (name = '{}', extract_dir = '{}'",
                 *this, name, extract_dir.value_or("(empty)"));
}

Result<std::set<std::string, std::less<void>>> AndroidBuild::AbPartitions() {
  return CF_ERRF("Unimplemented for '{}'", *this);
}

Result<std::set<std::string, std::less<void>>>
AndroidBuild::SystemPartitions() {
  return CF_ERRF("Unimplemented for '{}'", *this);
}

Result<std::set<std::string, std::less<void>>>
AndroidBuild::VendorPartitions() {
  return CF_ERRF("Unimplemented for '{}'", *this);
}

Result<std::set<std::string, std::less<void>>>
AndroidBuild::LogicalPartitions() {
  return CF_ERRF("Unimplemented for '{}'", *this);
}

Result<std::set<std::string, std::less<void>>>
AndroidBuild::PhysicalPartitions() {
  return CF_ERRF("Unimplemented for '{}'", *this);
}

std::ostream& operator<<(std::ostream& out, const AndroidBuild& build) {
  return build.Format(out);
}

}  // namespace cuttlefish
