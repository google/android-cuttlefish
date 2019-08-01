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

namespace {

std::chrono::steady_clock::duration REFRESH_WINDOW =
    std::chrono::minutes(2);
std::string REFRESH_URL = "http://metadata.google.internal/computeMetadata/"
    "v1/instance/service-accounts/default/token";

}

GceMetadataCredentialSource::GceMetadataCredentialSource() {
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
  Json::Value credential_json =
      curl.DownloadToJson(REFRESH_URL, {"Metadata-Flavor: Google"});
  expiration = std::chrono::steady_clock::now()
      + std::chrono::seconds(credential_json["expires_in"].asInt());
  latest_credential = credential_json["access_token"].asString();
}

std::unique_ptr<CredentialSource> GceMetadataCredentialSource::make() {
  return std::unique_ptr<CredentialSource>(new GceMetadataCredentialSource());
}
