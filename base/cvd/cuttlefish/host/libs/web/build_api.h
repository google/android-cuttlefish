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

#include <optional>
#include <ostream>
#include <string>
#include <tuple>
#include <variant>
#include <vector>

#include "common/libs/utils/result.h"
#include "host/libs/web/android_build_string.h"

namespace cuttlefish {

struct DeviceBuild {
  DeviceBuild(std::string id, std::string target,
              std::optional<std::string> filepath);

  std::string id;
  std::string target;
  std::string product;
  std::optional<std::string> filepath;
};

std::ostream& operator<<(std::ostream&, const DeviceBuild&);

struct DirectoryBuild {
  // TODO(schuffelen): Support local builds other than "eng"
  DirectoryBuild(std::vector<std::string> paths, std::string target,
                 std::optional<std::string> filepath);

  std::vector<std::string> paths;
  std::string target;
  std::string id;
  std::string product;
  std::optional<std::string> filepath;
};

std::ostream& operator<<(std::ostream&, const DirectoryBuild&);

using Build = std::variant<DeviceBuild, DirectoryBuild>;

std::ostream& operator<<(std::ostream&, const Build&);

class IBuildApi {
 public:
  virtual ~IBuildApi() = default;
  virtual Result<Build> GetBuild(const BuildString& build_string,
                         const std::string& fallback_target) = 0;

  virtual Result<std::string> DownloadFile(const Build& build,
                                           const std::string& target_directory,
                                           const std::string& artifact_name) = 0;

  virtual Result<std::string> DownloadFileWithBackup(
      const Build& build, const std::string& target_directory,
      const std::string& artifact_name,
      const std::string& backup_artifact_name) = 0;

  virtual Result<std::string> GetBuildZipName(const Build& build, const std::string& name) = 0;

};

}  // namespace cuttlefish
