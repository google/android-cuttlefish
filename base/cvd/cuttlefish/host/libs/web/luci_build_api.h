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

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "common/libs/utils/result.h"
#include "host/libs/web/chrome_os_build_string.h"
#include "host/libs/web/credential_source.h"
#include "host/libs/web/http_client/http_client.h"

namespace cuttlefish {

struct ChromeOsBuildArtifacts {
  std::string artifact_link;
  std::vector<std::string> artifact_files;
};

class LuciBuildApi {
 public:
  LuciBuildApi();
  LuciBuildApi(std::unique_ptr<HttpClient> http_client,
               std::unique_ptr<HttpClient> inner_http_client,
               std::unique_ptr<CredentialSource> buildbucket_credential_source,
               std::unique_ptr<CredentialSource> storage_credential_source);

  Result<std::optional<ChromeOsBuildArtifacts>> GetBuildArtifacts(
      const ChromeOsBuildString&);

  Result<void> DownloadArtifact(const std::string& artifact_link,
                                const std::string& artifact_file,
                                const std::string& target_path);

 private:
  Result<std::vector<std::string>> BuildBucketHeaders();
  Result<std::vector<std::string>> CloudStorageHeaders();

  std::unique_ptr<HttpClient> http_client_;
  std::unique_ptr<HttpClient> inner_http_client_;
  std::unique_ptr<CredentialSource> buildbucket_credential_source_;
  std::unique_ptr<CredentialSource> storage_credential_source_;
};

}  // namespace cuttlefish
