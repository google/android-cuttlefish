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

#include <json/json.h>
#include <openssl/evp.h>

#include "common/libs/utils/result.h"
#include "host/libs/web/curl_wrapper.h"

namespace cuttlefish {

class CredentialSource {
public:
  virtual ~CredentialSource() = default;
  virtual std::string Credential() = 0;
};

class GceMetadataCredentialSource : public CredentialSource {
  CurlWrapper& curl;
  std::string latest_credential;
  std::chrono::steady_clock::time_point expiration;

  void RefreshCredential();
public:
 GceMetadataCredentialSource(CurlWrapper&);
 GceMetadataCredentialSource(GceMetadataCredentialSource&&) = default;

 virtual std::string Credential();

 static std::unique_ptr<CredentialSource> make(CurlWrapper&);
};

class FixedCredentialSource : public CredentialSource {
  std::string credential;
public:
  FixedCredentialSource(const std::string& credential);

  virtual std::string Credential();

  static std::unique_ptr<CredentialSource> make(const std::string& credential);
};

class RefreshCredentialSource : public CredentialSource {
 public:
  static Result<RefreshCredentialSource> FromOauth2ClientFile(
      CurlWrapper& curl, std::istream& stream);

  RefreshCredentialSource(CurlWrapper& curl, const std::string& client_id,
                          const std::string& client_secret,
                          const std::string& refresh_token);

  std::string Credential() override;

 private:
  void UpdateLatestCredential();

  CurlWrapper& curl_;
  std::string client_id_;
  std::string client_secret_;
  std::string refresh_token_;

  std::string latest_credential_;
  std::chrono::steady_clock::time_point expiration_;
};

class ServiceAccountOauthCredentialSource : public CredentialSource {
 public:
  static Result<ServiceAccountOauthCredentialSource> FromJson(
      CurlWrapper& curl, const Json::Value& service_account_json,
      const std::string& scope);
  ServiceAccountOauthCredentialSource(ServiceAccountOauthCredentialSource&&) =
      default;

  std::string Credential() override;

 private:
  ServiceAccountOauthCredentialSource(CurlWrapper& curl);
  void RefreshCredential();

  CurlWrapper& curl_;
  std::string email_;
  std::string scope_;
  std::unique_ptr<EVP_PKEY, void (*)(EVP_PKEY*)> private_key_;

  std::string latest_credential_;
  std::chrono::steady_clock::time_point expiration_;
};
}
