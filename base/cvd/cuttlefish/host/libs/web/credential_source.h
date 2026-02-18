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
#include <mutex>
#include <string>
#include <utility>

#include <openssl/evp.h>

#include "cuttlefish/common/libs/utils/json.h"
#include "cuttlefish/host/libs/web/http_client/http_client.h"
#include "cuttlefish/result/result.h"

namespace cuttlefish {

class CredentialSource {
 public:
  virtual ~CredentialSource() = default;
  virtual Result<std::string> Credential() = 0;
};

// Credentials with known expiration times with behavior to load new
// credentials.
class RefreshingCredentialSource : public CredentialSource {
 public:
  virtual Result<std::string> Credential() final override;

 protected:
  RefreshingCredentialSource();

 private:
  virtual Result<std::pair<std::string, std::chrono::seconds>> Refresh() = 0;

  std::string latest_credential_;
  std::mutex latest_credential_mutex_;
  std::chrono::steady_clock::time_point expiration_;
};

// OAuth2 credentials from the GCE metadata server.
//
// -
// https://cloud.google.com/compute/docs/access/authenticate-workloads#applications
// - https://cloud.google.com/compute/docs/metadata/overview
class GceMetadataCredentialSource : public RefreshingCredentialSource {
 public:
  static std::unique_ptr<CredentialSource> Make(HttpClient&);

 private:
  GceMetadataCredentialSource(HttpClient&);

  Result<std::pair<std::string, std::chrono::seconds>> Refresh() override;

  HttpClient& http_client_;
};

// Pass through a string as an authentication token with unknown expiration.
class FixedCredentialSource : public CredentialSource {
 public:
  Result<std::string> Credential() override;

  static std::unique_ptr<CredentialSource> Make(const std::string& credential);

 private:
  FixedCredentialSource(const std::string& credential);
  std::string credential_;
};

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

// OAuth2 tokens from service account files.
//
// https://developers.google.com/identity/protocols/oauth2/service-account
class ServiceAccountOauthCredentialSource : public RefreshingCredentialSource {
 public:
  static Result<std::unique_ptr<ServiceAccountOauthCredentialSource>> FromJson(
      HttpClient& http_client, const Json::Value& service_account_json,
      const std::string& scope);

 private:
  ServiceAccountOauthCredentialSource(HttpClient& http_client);

  Result<std::pair<std::string, std::chrono::seconds>> Refresh() override;

  HttpClient& http_client_;
  std::string email_;
  std::string scope_;
  std::unique_ptr<EVP_PKEY, void (*)(EVP_PKEY*)> private_key_;
};

}  // namespace cuttlefish
