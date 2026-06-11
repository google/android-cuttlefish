//
// Copyright (C) 2026 The Android Open Source Project
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

#include "cuttlefish/host/libs/web/build_api.h"

#include <string>

#include "cuttlefish/host/libs/web/android_build.h"
#include "cuttlefish/result/result.h"

namespace cuttlefish {

Result<std::string> DownloadFileWithBackup(
    BuildApi& build_api, const Build& build,
    const std::string& target_directory, const std::string& artifact_name,
    const std::string& backup_artifact_name) {
  Result<std::string> result =
      build_api.DownloadFile(build, target_directory, artifact_name);
  if (result.ok()) {
    return result;
  }
  return CF_EXPECT(
      build_api.DownloadFile(build, target_directory, backup_artifact_name));
}

}  // namespace cuttlefish
