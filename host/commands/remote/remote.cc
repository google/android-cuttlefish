//
// Copyright (C) 2022 The Android Open Source Project
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

#include "host/commands/remote/remote.h"

#include "common/libs/utils/json.h"

namespace cuttlefish {
namespace {

const char* kFieldItems = "items";

}  // namespace

CloudOrchestratorApi::CloudOrchestratorApi(const std::string& service_url,
                                           const std::string& zone,
                                           HttpClient& http_client)
    : service_url_(service_url), zone_(zone), http_client_(http_client) {}

CloudOrchestratorApi::~CloudOrchestratorApi() {}

Result<std::vector<std::string>> CloudOrchestratorApi::ListHosts() {
  std::string url = service_url_ + "/v1/zones/" + zone_ + "/hosts";
  auto resp = CF_EXPECT(http_client_.GetToString(url), "Http client failed");
  CF_EXPECT(resp.HttpSuccess(), "Http request failed with status code: "
                                    << resp.http_code << ", server response:\n"
                                    << resp.data);
  auto root = CF_EXPECT(ParseJson(resp.data), "Failed parsing response body");
  CF_EXPECT(
      root.isMember(kFieldItems),
      "Invalid list hosts response,  missing field: '" << kFieldItems << "'");
  std::vector<std::string> result;
  for (const Json::Value& item : root[kFieldItems]) {
    result.push_back(item["name"].asString());
  }
  return result;
}

}  // namespace cuttlefish
