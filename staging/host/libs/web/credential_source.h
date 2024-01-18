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
#include <istream>
#include <memory>
#include <string>

#include <json/json.h>
#include <openssl/evp.h>

#include "common/libs/utils/result.h"
#include "host/libs/web/http_client/http_client.h"

namespace cuttlefish {

inline constexpr char kBuildScope[] =
    "https://www.googleapis.com/auth/androidbuild.internal";

class CredentialSource {
public:
  virtual ~CredentialSource() = default;
  virtual Result<std::string> Credential() = 0;
};

class GceMetadataCredentialSource : public CredentialSource {
  HttpClient& http_client;
  std::string latest_credential;
  std::chrono::steady_clock::time_point expiration;

  Result<void> RefreshCredential();

 public:
  GceMetadataCredentialSource(HttpClient&);
  GceMetadataCredentialSource(GceMetadataCredentialSource&&) = default;

  Result<std::string> Credential() override;

  static std::unique_ptr<CredentialSource> Make(HttpClient&);
};

class FixedCredentialSource : public CredentialSource {
  std::string credential;
public:
  FixedCredentialSource(const std::string& credential);

  Result<std::string> Credential() override;

  static std::unique_ptr<CredentialSource> Make(const std::string& credential);
};

class RefreshCredentialSource : public CredentialSource {
 public:
  static Result<RefreshCredentialSource> FromOauth2ClientFile(
      HttpClient& http_client, std::istream& stream);

  RefreshCredentialSource(HttpClient& http_client, const std::string& client_id,
                          const std::string& client_secret,
                          const std::string& refresh_token);

  Result<std::string> Credential() override;

 private:
  Result<void> UpdateLatestCredential();

  HttpClient& http_client_;
  std::string client_id_;
  std::string client_secret_;
  std::string refresh_token_;

  std::string latest_credential_;
  std::chrono::steady_clock::time_point expiration_;
};

class ServiceAccountOauthCredentialSource : public CredentialSource {
 public:
  static Result<ServiceAccountOauthCredentialSource> FromJson(
      HttpClient& http_client, const Json::Value& service_account_json,
      const std::string& scope);
  ServiceAccountOauthCredentialSource(ServiceAccountOauthCredentialSource&&) =
      default;

  Result<std::string> Credential() override;

 private:
  ServiceAccountOauthCredentialSource(HttpClient& http_client);
  Result<void> RefreshCredential();

  HttpClient& http_client_;
  std::string email_;
  std::string scope_;
  std::unique_ptr<EVP_PKEY, void (*)(EVP_PKEY*)> private_key_;

  std::string latest_credential_;
  std::chrono::steady_clock::time_point expiration_;
};

Result<std::unique_ptr<CredentialSource>> GetCredentialSource(
    HttpClient& http_client, const std::string& credential_source,
    const std::string& oauth_filepath);
}
