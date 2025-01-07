//
// Copyright (C) 2024 The Android Open Source Project
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

#include <string>
#include <vector>

#include "common/libs/utils/result.h"

namespace cuttlefish {

// See https://specifications.freedesktop.org/basedir-spec/latest/

Result<std::string> CvdDataHome();
Result<std::string> CvdConfigHome();
Result<std::string> CvdStateHome();
Result<std::string> CvdCacheHome();
std::string CvdRuntimeDir();

Result<std::vector<std::string>> CvdDataDirs();
Result<std::vector<std::string>> CvdConfigDirs();

Result<std::string> ReadCvdDataFile(std::string_view path);
Result<std::vector<std::string>> FindCvdDataFiles(std::string_view path);
Result<void> WriteCvdDataFile(std::string_view path, std::string contents);

// TODO: schuffelen - Decide between merging or overriding for config files

}  // namespace cuttlefish
