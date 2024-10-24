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

#include <chrono>
#include <memory>
#include <string>

#include "common/libs/utils/result.h"
#include "host/libs/web/android_build_api.h"
#include "host/libs/web/credential_source.h"
#include "host/libs/web/http_client/http_client.h"

namespace cuttlefish {

class CachingBuildApi : public IBuildApi {
 public:
  CachingBuildApi() = delete;
  CachingBuildApi(CachingBuildApi&&) = delete;
  ~CachingBuildApi() override = default;
  CachingBuildApi(std::unique_ptr<IBuildApi> build_api,
                  const std::string cache_base_path);

  Result<Build> GetBuild(const BuildString& build_string,
                         const std::string& fallback_target);
  Result<std::string> DownloadFile(const Build& build,
                                   const std::string& target_directory,
                                   const std::string& artifact_name) override;
  Result<std::string> DownloadFileWithBackup(
      const Build& build, const std::string& target_directory,
      const std::string& artifact_name,
      const std::string& backup_artifact_name) override;

  Result<std::string> GetBuildZipName(const Build& build, const std::string& name);

 private:
  Result<bool> CanCache(const std::string& target_directory);

  std::unique_ptr<IBuildApi> build_api_;
  std::string cache_base_path_;
};

std::unique_ptr<IBuildApi> CreateBuildApi(
    std::unique_ptr<HttpClient> http_client,
    std::unique_ptr<HttpClient> inner_http_client,
    std::unique_ptr<CredentialSource> credential_source, std::string api_key,
    const std::chrono::seconds retry_period, std::string api_base_url, std::string project_id,
    const bool enable_caching, const std::string cache_base_path);

}  // namespace cuttlefish
