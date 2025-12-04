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

#include <string>
#include <string_view>

#include "cuttlefish/common/libs/utils/result.h"
#include "cuttlefish/host/libs/config/build_archive.h"
#include "cuttlefish/host/libs/config/fetcher_config.h"
#include "cuttlefish/host/libs/config/file_source.h"

namespace cuttlefish {

/**
 * Scan a `FetcherConfig` for an archive containing the substring `pattern`.
 * This could be a zip file still present, or the extracted contents of a zip
 * file that was downloaded.
 */
Result<BuildArchive> FindBuildArchive(const FetcherConfig&, FileSource,
                                      std::string_view pattern);
/**
 * Scan the contents of `directory_path` to find a file whose name contains
 * `pattern` as a substring, and is a zip archive.
 */
Result<BuildArchive> FindBuildArchive(const std::string& directory_path,
                                      std::string_view pattern);

}  // namespace cuttlefish
