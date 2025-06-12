//
// Copyright (C) 2023 The Android Open Source Project
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

#include <optional>
#include <string>
#include <vector>

#include "cuttlefish/common/libs/utils/result.h"
#include "cuttlefish/host/commands/cvd/fetch/build_api_flags.h"
#include "cuttlefish/host/commands/cvd/fetch/vector_flags.h"

namespace cuttlefish {

inline constexpr char kDefaultBuildString[] = "";
inline constexpr char kDefaultTargetDirectory[] = "";
inline constexpr bool kDefaultKeepDownloadedArchives = false;

inline constexpr char kDefaultBuildTarget[] =
    "aosp_cf_x86_64_only_phone-userdebug";

struct FetchFlags {
  std::string target_directory = kDefaultTargetDirectory;
  std::optional<BuildString> host_package_build;
  bool keep_downloaded_archives = kDefaultKeepDownloadedArchives;
  android::base::LogSeverity verbosity = android::base::INFO;
  bool helpxml = false;
  BuildApiFlags build_api_flags;
  VectorFlags vector_flags;
  std::vector<std::string> host_substitutions;

  static Result<FetchFlags> Parse(std::vector<std::string>& args);
};

}  // namespace cuttlefish
