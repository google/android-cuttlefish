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

#include "cuttlefish/host/libs/metrics/metrics_transmitter.h"

#include "cuttlefish/common/libs/utils/result.h"
#include "cuttlefish/host/libs/web/http_client/curl_global_init.h"
#include "cuttlefish/host/libs/web/http_client/curl_http_client.h"
#include "cuttlefish/host/libs/web/http_client/http_client.h"
#include "cuttlefish/host/libs/web/http_client/http_string.h"
#include "external_proto/clientanalytics.pb.h"

namespace cuttlefish {
namespace {

// TODO: chadreynolds - create a compilation or runtime flag to swap
// environments
enum class ClearcutEnvironment {
  kLocal = 0,
  kStaging = 1,
  kProd = 2,
};

std::string ClearcutEnvironmentUrl(const ClearcutEnvironment environment) {
  switch (environment) {
    case ClearcutEnvironment::kLocal:
      return "http://localhost:27910/log";
    case ClearcutEnvironment::kStaging:
      return "https://play.googleapis.com:443/staging/log";
    case ClearcutEnvironment::kProd:
      return "https://play.googleapis.com:443/log";
  }
}

Result<void> PostRequest(HttpClient& http_client, const std::string& output,
                         const ClearcutEnvironment server) {
  const std::string clearcut_url = ClearcutEnvironmentUrl(server);
  HttpResponse<std::string> response =
      CF_EXPECT(HttpPostToString(http_client, clearcut_url, output));
  CF_EXPECTF(response.HttpSuccess(), "Metrics POST failed ({}): {}",
             response.http_code, response.data);
  return {};
}

}  // namespace

Result<void> TransmitMetricsEvent(
    const wireless_android_play_playlog::LogRequest& log_request) {
  CurlGlobalInit curl_global_init;
  std::unique_ptr<HttpClient> http_client = CurlHttpClient();
  CF_EXPECT(http_client.get() != nullptr,
            "Unable to create cURL client for metrics transmission");
  CF_EXPECT(PostRequest(*http_client, log_request.SerializeAsString(),
                        ClearcutEnvironment::kProd));
  return {};
}

}  // namespace cuttlefish
