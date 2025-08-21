//
// Copyright (C) 2019 The Android Open Source Project
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

#include "cuttlefish/host/libs/web/android_build.h"

#include <optional>
#include <ostream>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include <android-base/strings.h>

#include "cuttlefish/common/libs/utils/environment.h"

namespace cuttlefish {

std::ostream& operator<<(std::ostream& out, const DeviceBuild& build) {
  return out << "(id=\"" << build.id << "\", target=\"" << build.target
             << "\", filepath=\"" << build.filepath.value_or("") << "\")";
}

DirectoryBuild::DirectoryBuild(std::vector<std::string> paths,
                               std::string target,
                               std::optional<std::string> filepath)
    : paths(std::move(paths)),
      target(std::move(target)),
      // TODO(schuffelen): Support local builds other than "eng"
      id("eng"),
      filepath(std::move(filepath)) {
  product = StringFromEnv("TARGET_PRODUCT", "");
}

std::ostream& operator<<(std::ostream& out, const DirectoryBuild& build) {
  auto paths = android::base::Join(build.paths, ":");
  return out << "(paths=\"" << paths << "\", target=\"" << build.target
             << "\", filepath=\"" << build.filepath.value_or("") << "\")";
}

std::ostream& operator<<(std::ostream& out, const Build& build) {
  std::visit([&out](auto&& arg) { out << arg; }, build);
  return out;
}

}  // namespace cuttlefish
