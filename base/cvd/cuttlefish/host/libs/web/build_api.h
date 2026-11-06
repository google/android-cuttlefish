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

#include "cuttlefish/host/libs/zip/libzip_cc/seekable_source.h"
#include "cuttlefish/result/result_type.h"

namespace cuttlefish {

template <typename StringType, typename BuildType>
class BuildApi {
 public:
  virtual ~BuildApi() = default;
  virtual Result<BuildType> GetBuild(const StringType& build_string) = 0;

  virtual Result<std::string> DownloadFile(
      const BuildType& build, const std::string& target_directory,
      const std::string& artifact_name) = 0;

  virtual Result<SeekableZipSource> FileReader(
      const BuildType&, const std::string& artifact_name) = 0;
};

}  // namespace cuttlefish
