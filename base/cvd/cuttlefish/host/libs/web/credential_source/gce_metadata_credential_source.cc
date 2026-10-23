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

#include "cuttlefish/host/libs/web/credential_source/gce_metadata_credential_source.h"

#include <chrono>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "json/value.h"
#include "json/writer.h"

#include "cuttlefish/host/libs/web/credential_source/credential_source.h"
#include "cuttlefish/host/libs/web/http_client/http_client.h"
#include "cuttlefish/host/libs/web/http_client/http_json.h"
#include "cuttlefish/result/expect.h"
#include "cuttlefish/result/result_type.h"

namespace cuttlefish {

GceMetadataCredentialSource::GceMetadataCredentialSource(
    HttpClient& http_client)
    : http_client_(http_client) {}

Result<std::pair<std::string, std::chrono::seconds>>
GceMetadataCredentialSource::Refresh() {
  static constexpr char kRefreshUrl[] =
      "http://metadata.google.internal/computeMetadata/v1/instance/"
      "service-accounts/default/token";
  auto response = CF_EXPECT(
      HttpGetToJson(http_client_, kRefreshUrl, {"Metadata-Flavor: Google"}));
  const auto& json = response.data;
  CF_EXPECT(response.HttpSuccess(),
            "Error fetching credentials. The server response was \""
                << json << "\", and code was " << response.http_code);
  CF_EXPECT(!json.isMember("error"),
            "Response had \"error\" but had http success status. Received \""
                << json << "\"");

  CF_EXPECT(json.isMember("access_token") && json.isMember("expires_in"),
            "GCE credential was missing access_token or expires_in. "
                << "Full response was " << json << "");

  return {{json["access_token"].asString(),
           std::chrono::seconds(json["expires_in"].asInt())}};
}

std::unique_ptr<CredentialSource> GceMetadataCredentialSource::Make(
    HttpClient& http_client) {
  return std::unique_ptr<CredentialSource>(
      new GceMetadataCredentialSource(http_client));
}

}  // namespace cuttlefish
