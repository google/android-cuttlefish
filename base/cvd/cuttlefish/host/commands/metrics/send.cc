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

#include "cuttlefish/host/commands/metrics/send.h"

#include <string>

#include <android-base/logging.h>

#include "cuttlefish/host/libs/metrics/metrics_defs.h"
#include "cuttlefish/host/libs/web/http_client/curl_global_init.h"
#include "cuttlefish/host/libs/web/http_client/curl_http_client.h"
#include "cuttlefish/host/libs/web/http_client/http_client.h"
#include "cuttlefish/host/libs/web/http_client/http_string.h"
#include "cuttlefish/result/result.h"

namespace cuttlefish::metrics {
namespace {

std::string ClearcutServerUrl(ClearcutServer server) {
  switch (server) {
    case ClearcutServer::kLocal:
      return "http://localhost:27910/log";

    case ClearcutServer::kStaging:
      return "https://play.googleapis.com:443/staging/log";

    case ClearcutServer::kProd:
      return "https://play.googleapis.com:443/log";

    default:
      LOG(FATAL) << "Invalid host configuration";
      return "";
  }
}

}  // namespace

MetricsExitCodes PostRequest(const std::string& output, ClearcutServer server) {
  CurlGlobalInit curl_global_init;
  std::unique_ptr<HttpClient> http_client = CurlHttpClient();
  if (!http_client) {
    return MetricsExitCodes::kMetricsError;
  }
  return PostRequest(*http_client, output, server);
}

MetricsExitCodes PostRequest(HttpClient& http_client, const std::string& output,
                             ClearcutServer server) {
  std::string clearcut_url = ClearcutServerUrl(server);

  Result<HttpResponse<std::string>> http_res =
      HttpPostToString(http_client, clearcut_url, output);
  if (!http_res.ok()) {
    LOG(ERROR) << "HTTP command failed: " << http_res.error().FormatForEnv();
    return MetricsExitCodes::kMetricsError;
  }

  if (!http_res->HttpSuccess()) {
    LOG(ERROR) << "Metrics message failed: [" << http_res->data << "]";
    LOG(ERROR) << "http error code: " << http_res->http_code;
    return MetricsExitCodes::kMetricsError;
  }
  LOG(INFO) << "Metrics posted to ClearCut";
  return MetricsExitCodes::kSuccess;
}

}  // namespace cuttlefish::metrics
