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

#include "credential_source.h"

#include <android-base/logging.h>

namespace cuttlefish {
namespace {

std::chrono::steady_clock::duration REFRESH_WINDOW =
    std::chrono::minutes(2);
std::string REFRESH_URL = "http://metadata.google.internal/computeMetadata/"
    "v1/instance/service-accounts/default/token";

} // namespace

GceMetadataCredentialSource::GceMetadataCredentialSource(CurlWrapper& curl)
    : curl(curl) {
  latest_credential = "";
  expiration = std::chrono::steady_clock::now();
}

std::string GceMetadataCredentialSource::Credential() {
  if (expiration - std::chrono::steady_clock::now() < REFRESH_WINDOW) {
    RefreshCredential();
  }
  return latest_credential;
}

void GceMetadataCredentialSource::RefreshCredential() {
  auto curl_response =
      curl.DownloadToJson(REFRESH_URL, {"Metadata-Flavor: Google"});
  const auto& json = curl_response.data;
  if (!curl_response.HttpSuccess()) {
    LOG(FATAL) << "Error fetching credentials. The server response was \""
               << json << "\", and code was " << curl_response.http_code;
  }
  CHECK(!json.isMember("error"))
      << "Response had \"error\" but had http success status. Received \""
      << json << "\"";

  bool has_access_token = json.isMember("access_token");
  bool has_expires_in = json.isMember("expires_in");
  if (!has_access_token || !has_expires_in) {
    LOG(FATAL) << "GCE credential was missing access_token or expires_in. "
               << "Full response was " << json << "";
  }

  expiration = std::chrono::steady_clock::now() +
               std::chrono::seconds(json["expires_in"].asInt());
  latest_credential = json["access_token"].asString();
}

std::unique_ptr<CredentialSource> GceMetadataCredentialSource::make(
    CurlWrapper& curl) {
  return std::unique_ptr<CredentialSource>(
      new GceMetadataCredentialSource(curl));
}

FixedCredentialSource::FixedCredentialSource(const std::string& credential) {
  this->credential = credential;
}

std::string FixedCredentialSource::Credential() {
  return credential;
}

std::unique_ptr<CredentialSource> FixedCredentialSource::make(
    const std::string& credential) {
  return std::unique_ptr<CredentialSource>(new FixedCredentialSource(credential));
}

} // namespace cuttlefish
