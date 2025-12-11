/*
 * Copyright (C) 2025 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "cuttlefish/host/commands/metrics/metrics_transmission.h"

#include <memory>
#include <string>

#include "cuttlefish/host/libs/metrics/metrics_environment.h"
#include "cuttlefish/host/libs/web/http_client/curl_global_init.h"
#include "cuttlefish/host/libs/web/http_client/curl_http_client.h"
#include "cuttlefish/host/libs/web/http_client/http_client.h"
#include "cuttlefish/host/libs/web/http_client/http_string.h"
#include "cuttlefish/result/result.h"
#include "external_proto/clientanalytics.pb.h"

namespace cuttlefish {
namespace {

std::string ClearcutEnvironmentUrl(const ClearcutEnvironment environment) {
  switch (environment) {
    case ClearcutEnvironment::Local:
      return "http://localhost:27910/log";
    case ClearcutEnvironment::Staging:
      return "https://play.googleapis.com:443/staging/log";
    case ClearcutEnvironment::Production:
      return "https://play.googleapis.com:443/log";
  }
}

Result<void> PostRequest(HttpClient& http_client, const std::string& output,
                         const ClearcutEnvironment server) {
  const std::string clearcut_url = ClearcutEnvironmentUrl(server);
  const HttpResponse<std::string> response =
      CF_EXPECT(HttpPostToString(http_client, clearcut_url, output));
  CF_EXPECTF(response.HttpSuccess(), "Metrics POST failed ({}): {}",
             response.http_code, response.data);
  return {};
}

}  // namespace

Result<void> TransmitMetricsEvent(
    const wireless_android_play_playlog::LogRequest& log_request,
    ClearcutEnvironment environment) {
  CurlGlobalInit curl_global_init;
  const bool use_logging_debug_function = true;
  std::unique_ptr<HttpClient> http_client =
      CurlHttpClient(use_logging_debug_function);
  CF_EXPECT(http_client.get() != nullptr,
            "Unable to create cURL client for metrics transmission");
  CF_EXPECT(
      PostRequest(*http_client, log_request.SerializeAsString(), environment));
  return {};
}

}  // namespace cuttlefish
