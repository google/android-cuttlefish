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

#include <string>
#include <string_view>
#include <vector>

#include "json/json.h"

#include "cuttlefish/host/libs/web/http_client/http_client.h"
#include "cuttlefish/result/result.h"

namespace cuttlefish {

// Returns the json object contained in the response's body.
//
// NOTE: In case of a parsing error a successful `result` will be returned
// with the relevant http status code and a json object with the next format:
// {
//   "error": "Failed to parse json",
//   "response: "<THE RESPONSE BODY>"
// }
Result<HttpResponse<Json::Value>> HttpPostToJson(
    HttpClient&, const std::string& url, const std::string& data,
    const std::vector<std::string>& headers = {});
Result<HttpResponse<Json::Value>> HttpPostToJson(
    HttpClient&, const std::string& url, const Json::Value& data,
    const std::vector<std::string>& headers = {});
Result<HttpResponse<Json::Value>> HttpGetToJson(
    HttpClient&, const std::string& url,
    const std::vector<std::string>& headers = {});

// What differs between the APIs that answer a request with json.
struct JsonResponseOptions {
  // Names the API in the error of a response that failed.
  std::string_view source;
  // Whether a redirect carries the json the caller asked for.
  bool allow_redirect = false;
  // Whether an "error" member of a successful response is itself a failure.
  bool reject_error_member = false;
  // Appended to the error of a response that failed.
  std::string_view hint;
};

// Logs the body of `response` and returns its json, or an error naming the
// status the API answered with.
Result<Json::Value> JsonFromResponse(const HttpResponse<Json::Value>& response,
                                     const JsonResponseOptions& options);

}  // namespace cuttlefish
