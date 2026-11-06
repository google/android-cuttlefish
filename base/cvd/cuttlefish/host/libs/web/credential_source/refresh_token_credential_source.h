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

#include <chrono>
#include <memory>
#include <string>
#include <utility>

#include "json/value.h"

#include "cuttlefish/host/libs/web/credential_source/credential_source.h"
#include "cuttlefish/host/libs/web/credential_source/refreshing_credential_source.h"
#include "cuttlefish/host/libs/web/http_client/http_client.h"
#include "cuttlefish/result/result_type.h"

namespace cuttlefish {

// OAuth2 tokens from a desktop refresh token.
//
// https://developers.google.com/identity/protocols/oauth2/native-app
class RefreshTokenCredentialSource : public RefreshingCredentialSource {
 public:
  static Result<std::unique_ptr<RefreshTokenCredentialSource>>
  FromOauth2ClientFile(HttpClient& http_client,
                       const std::string& oauth_contents);

  static Result<std::unique_ptr<CredentialSource>> Make(
      HttpClient& http_client, const std::string& client_id,
      const std::string& client_secret, const std::string& refresh_token);

 private:
  RefreshTokenCredentialSource(HttpClient& http_client,
                               const std::string& client_id,
                               const std::string& client_secret,
                               const std::string& refresh_token);

  static Result<std::unique_ptr<RefreshTokenCredentialSource>> FromJson(
      HttpClient& http_client, const Json::Value& credential);

  Result<std::pair<std::string, std::chrono::seconds>> Refresh() override;

  HttpClient& http_client_;
  std::string client_id_;
  std::string client_secret_;
  std::string refresh_token_;
};

}  // namespace cuttlefish
