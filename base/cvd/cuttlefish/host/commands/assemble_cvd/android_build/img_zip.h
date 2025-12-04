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

namespace cuttlefish {

/**
 * Represents the <build target>-img-<build id>.zip file produced by the Android
 * build system, possibly in an extracted form produced by `cvd fetch`.
 *
 * Expected to contain `super.img` and other `.img` files that are physical
 * partitions. Does not contain `.img` files for the logical partitions inside
 * `super`.
 *
 * Also expected to contain the `android-info.txt` file and/or the
 * `cuttlefish-guest-config.txtpb` file. See go/cf-guest-config-pb for more
 * information.
 */
Result<std::unique_ptr<AndroidBuild>> ImgZip(const FetcherConfig& config,
                                             FileSource source);

Result<std::unique_ptr<AndroidBuild>> ImgZip(const std::string& path);

}  // namespace cuttlefish
