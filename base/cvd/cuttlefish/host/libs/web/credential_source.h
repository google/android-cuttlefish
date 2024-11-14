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

#include <memory>
#include <string>

#include "common/libs/utils/result.h"
#include "host/libs/web/http_client/http_client.h"

namespace cuttlefish {

constexpr char kAndroidBuildApiScope[] =
    "https://www.googleapis.com/auth/androidbuild.internal";

class CredentialSource {
 public:
  virtual ~CredentialSource() = default;
  virtual Result<std::string> Credential() = 0;
};

Result<std::unique_ptr<CredentialSource>> GetCredentialSource(
    HttpClient& http_client, const std::string& credential_source,
    const std::string& oauth_filepath, bool use_gce_metadata,
    const std::string& credential_filepath,
    const std::string& service_account_filepath);

Result<std::unique_ptr<CredentialSource>> CreateRefreshTokenCredentialSource(
    HttpClient& http_client, const std::string& client_id,
    const std::string& client_secret, const std::string& refresh_token);
}
