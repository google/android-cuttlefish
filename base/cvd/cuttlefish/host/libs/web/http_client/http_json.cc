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

#include "cuttlefish/host/libs/web/http_client/http_json.h"

#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "absl/log/log.h"
#include "json/value.h"

#include "cuttlefish/common/libs/utils/json.h"
#include "cuttlefish/host/libs/web/http_client/http_client.h"
#include "cuttlefish/host/libs/web/http_client/http_string.h"
#include "cuttlefish/host/libs/web/http_client/scrub_secrets.h"
#include "cuttlefish/result/result.h"

namespace cuttlefish {
namespace {

HttpResponse<Json::Value> Parse(HttpResponse<std::string> response) {
  Result<Json::Value> result = ParseJson(response.data);
  if (!result.has_value()) {
    Json::Value error_json;
    LOG(ERROR) << "Could not parse json: " << result.error();
    error_json["error"] = "Failed to parse json: " + result.error().Message();
    error_json["response"] = response.data;
    return HttpResponse<Json::Value>{.data = error_json,
                                     .http_code = response.http_code,
                                     .headers = std::move(response.headers)};
  }
  return HttpResponse<Json::Value>{.data = *result,
                                   .http_code = response.http_code,
                                   .headers = std::move(response.headers)};
}

}  // namespace

Result<HttpResponse<Json::Value>> HttpPostToJson(
    HttpClient& http_client, const std::string& url, const std::string& data,
    const std::vector<std::string>& headers) {
  return Parse(CF_EXPECT(HttpPostToString(http_client, url, data, headers)));
}

Result<HttpResponse<Json::Value>> HttpPostToJson(
    HttpClient& http_client, const std::string& url, const Json::Value& data,
    const std::vector<std::string>& headers) {
  std::stringstream json_str;
  json_str << data;
  return Parse(
      CF_EXPECT(HttpPostToString(http_client, url, json_str.str(), headers)));
}

Result<HttpResponse<Json::Value>> HttpGetToJson(
    HttpClient& http_client, const std::string& url,
    const std::vector<std::string>& headers) {
  return Parse(CF_EXPECT(HttpGetToString(http_client, url, headers)));
}

Result<Json::Value> JsonFromResponse(const HttpResponse<Json::Value>& response,
                                     const JsonResponseOptions& options) {
  //  debug information in error responses floods stderr with too much text
  //  logged at a level that still ends up in the log file
  //  the artifact response carries a `signedUrl` whose query string is the
  //  credential, so the body is scrubbed before it reaches the log file
  VLOG(0) << "API response data:\n"
          << ScrubSecrets(response.data.toStyledString());
  const bool response_code_allowed =
      response.HttpSuccess() ||
      (options.allow_redirect && response.HttpRedirect());
  CF_EXPECTF(std::move(response_code_allowed),
             "Error response from {} - {}:{}\nCheck log file for full "
             "response{}",
             options.source, response.http_code, response.StatusDescription(),
             options.hint);
  if (options.reject_error_member) {
    CF_EXPECT(!response.data.isMember("error"),
              "Response was successful, but contains error information.  Check "
              "log file for full response.");
  }
  return response.data;
}

}  // namespace cuttlefish
