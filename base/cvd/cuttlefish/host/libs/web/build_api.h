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

#pragma once

#include <string>

#include "cuttlefish/common/libs/utils/result.h"
#include "cuttlefish/host/libs/web/android_build.h"
#include "cuttlefish/host/libs/web/android_build_string.h"
#include "cuttlefish/host/libs/zip/zip_cc.h"

namespace cuttlefish {

class BuildApi {
 public:
  virtual ~BuildApi() = default;
  virtual Result<Build> GetBuild(const BuildString& build_string) = 0;

  virtual Result<std::string> DownloadFile(
      const Build& build, const std::string& target_directory,
      const std::string& artifact_name) = 0;

  virtual Result<std::string> DownloadFileWithBackup(
      const Build& build, const std::string& target_directory,
      const std::string& artifact_name,
      const std::string& backup_artifact_name) = 0;

  virtual Result<ReadableZip> OpenZipArchive(const Build& build,
                                             const std::string& artifact_name) = 0;
};

}  // namespace cuttlefish
