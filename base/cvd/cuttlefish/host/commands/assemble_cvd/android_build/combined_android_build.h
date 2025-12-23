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
#include <string>
#include <vector>

#include "cuttlefish/host/commands/assemble_cvd/android_build/android_build.h"
#include "cuttlefish/result/result.h"

namespace cuttlefish {

/**
 * Combines information from multiple `AndroidBuild` instances.
 *
 * Individual sources can implement parts of the API, and an instance of
 * this class combines non-error results to provide as much information as
 * possible to the caller.
 *
 * Instances provided to this class are expected to be part of the same build
 * and consistent with each other. Specifically, if files are from different
 * build targets, have different build ids, or are built from different sources,
 * they should not be combined with each other.
 */
Result<std::unique_ptr<AndroidBuild>> CombinedAndroidBuild(
    std::string name, std::vector<std::unique_ptr<AndroidBuild>>);

}  // namespace cuttlefish
