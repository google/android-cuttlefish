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

#include "cuttlefish/host/libs/web/http_client/http_string.h"

#include <string>
#include <vector>

#include "cuttlefish/common/libs/utils/result.h"
#include "cuttlefish/host/libs/web/http_client/http_client.h"

namespace cuttlefish {

Result<HttpResponse<std::string>> HttpGetToString(
    HttpClient& http_client, const std::string& url,
    const std::vector<std::string>& headers) {
  return CF_EXPECT(http_client.GetToString(url, headers));
}

Result<HttpResponse<std::string>> HttpPostToString(
    HttpClient& http_client, const std::string& url, const std::string& data,
    const std::vector<std::string>& headers) {
  return CF_EXPECT(http_client.PostToString(url, data, headers));
}

}  // namespace cuttlefish
