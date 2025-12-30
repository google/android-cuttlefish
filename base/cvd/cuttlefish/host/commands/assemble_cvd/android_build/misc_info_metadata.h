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

#pragma once

#include <functional>
#include <map>
#include <memory>
#include <string>

#include "cuttlefish/host/commands/assemble_cvd/android_build/android_build.h"
#include "cuttlefish/result/result.h"

namespace cuttlefish {

/**
 * Reports partition information from the key-value pairs in a `misc_info.txt`
 * file.
 *
 * Although `misc_info.txt` does not contain any image files, it does have a
 * complete list of the logical partitions that are intended to be present in a
 * complete `super.img` file, as well as the division between "system" and
 * "vendor" side logical partitions.
 */
Result<std::unique_ptr<AndroidBuild>> AndroidBuildFromMiscInfo(
    std::map<std::string, std::string, std::less<void>>);

}  // namespace cuttlefish
