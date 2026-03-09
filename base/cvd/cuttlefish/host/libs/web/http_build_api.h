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

#pragma once

#include <string>

#include "cuttlefish/host/libs/web/android_build.h"
#include "cuttlefish/host/libs/web/android_build_string.h"
#include "cuttlefish/host/libs/web/http_client/http_client.h"
#include "cuttlefish/host/libs/zip/libzip_cc/seekable_source.h"
#include "cuttlefish/result/result.h"

namespace cuttlefish {

// Handles build sources backed by public https:// or http:// URLs.
// Never sends authentication headers; callers must use GcsBuildApi (or a
// future authenticated build type) for credentialed sources.
class HttpBuildApi {
 public:
  explicit HttpBuildApi(HttpClient& http_client);

  Result<Build> GetBuild(const HttpBuildString& build_string);

  Result<std::string> DownloadFile(const HttpBuild& build,
                                   const std::string& target_directory,
                                   const std::string& artifact_name);

  Result<SeekableZipSource> FileReader(const HttpBuild& build,
                                       const std::string& artifact_name);

 private:
  HttpClient& http_client_;
};

}  // namespace cuttlefish
