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

#include "cuttlefish/host/commands/cvd/fetch/target_directories.h"

#include <string>
#include <vector>

#include "cuttlefish/host/commands/cvd/fetch/get_optional.h"

namespace cuttlefish {

TargetDirectories TargetDirectories::Create(
    const std::string& target_directory,
    const std::vector<std::string>& target_subdirectories, const int index,
    const bool append_subdirectory) {
  std::string base_directory = target_directory;
  if (append_subdirectory) {
    base_directory += "/" + GetOptional(target_subdirectories, index)
                                .value_or("instance_" + std::to_string(index));
  }
  return TargetDirectories{.root = base_directory,
                           .otatools = base_directory + "/otatools/",
                           .chrome_os = base_directory + "/chromeos"};
}

}  // namespace cuttlefish
