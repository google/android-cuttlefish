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
#include <vector>

#include "cuttlefish/host/libs/web/android_build.h"
#include "cuttlefish/host/libs/web/android_build_string.h"
#include "cuttlefish/host/libs/web/build_api.h"
#include "cuttlefish/host/libs/web/credential_source.h"
#include "cuttlefish/host/libs/web/http_client/http_client.h"
#include "cuttlefish/host/libs/zip/libzip_cc/seekable_source.h"
#include "cuttlefish/result/result.h"

namespace cuttlefish {

// Reads builds held in Cloud Storage through the JSON API. The credential
// source is optional because public buckets are readable without one.
class GcsBuildApi : public BuildApi {
 public:
  GcsBuildApi(HttpClient& http_client, CredentialSource* credential_source);

  Result<Build> GetBuild(const BuildString& build_string) override;

  Result<std::string> DownloadFile(const Build& build,
                                   const std::string& target_directory,
                                   const std::string& artifact_name) override;

  Result<SeekableZipSource> FileReader(
      const Build& build, const std::string& artifact_name) override;

 private:
  Result<std::vector<std::string>> Headers();

  Result<Build> GetBuild(const GcsBuildString& build_string);
  Result<void> ListContents(GcsBuild& build);
  Result<void> ProbeObject(GcsBuild& build);

  Result<std::string> DownloadFile(const GcsBuild& build,
                                   const std::string& target_directory,
                                   const std::string& artifact_name);
  Result<SeekableZipSource> FileReader(const GcsBuild& build,
                                       const std::string& artifact_name);

  HttpClient& http_client_;
  CredentialSource* credential_source_;
};

}  // namespace cuttlefish
