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

#include "cuttlefish/host/commands/assemble_cvd/android_build/android_build.h"
#include "cuttlefish/host/libs/config/fetcher_config.h"
#include "cuttlefish/host/libs/config/file_source.h"
#include "cuttlefish/result/result.h"

namespace cuttlefish {

/**
 * Finds Android build artifacts from a *-target_files-* zip file downloaded and
 * possibly extracted by `cvd fetch`.
 */
Result<std::unique_ptr<AndroidBuild>> TargetFiles(const FetcherConfig&,
                                                  FileSource);

/**
 * Finds android build artifacts from a *-target_files-* zip file that is
 * present in a directory, likely the `out/dist` directory of a local Android
 * build.
 */
Result<std::unique_ptr<AndroidBuild>> TargetFiles(const std::string& path);

}  // namespace cuttlefish
