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

#include <memory>

#include "cuttlefish/host/commands/assemble_cvd/android_build/android_build.h"
#include "cuttlefish/result/result.h"

namespace cuttlefish {

/**
 * Wrap an `AndroidBuild` with fallback physical partition detection logic.
 *
 * If the `AndroidBuild` does not already provide GPT entry information, this
 * makes a best guess based on known logical partitions and image files.
 *
 * One use case is the android product directory or `m` case, where the build
 * system produces a collection of `.img` files including a `super.img`, but no
 * explicit list of physical partitions.
 */
Result<std::unique_ptr<AndroidBuild>> PhysicalPartitions(AndroidBuild&);

}  // namespace cuttlefish
