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
#include <ostream>
#include <set>
#include <string>
#include <string_view>

#include "cuttlefish/result/result.h"

namespace cuttlefish {

Result<std::set<std::string, std::less<void>>> AndroidBuild::Images() {
  return CF_ERRF("Unimplemented for '{}'", *this);
}

Result<std::string> AndroidBuild::ImageFile(std::string_view name,
                                            bool extract) {
  return CF_ERRF("Unimplemented for '{}': (name = '{}', extract = {})", *this,
                 name, extract);
}

Result<void> AndroidBuild::SetExtractDir(std::string_view dir) {
  return CF_ERRF("Unimplemented for '{}': (dir = '{}'", *this, dir);
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
  return out << build.Name();
}

std::string format_as(const AndroidBuild& build) { return build.Name(); }

}  // namespace cuttlefish
