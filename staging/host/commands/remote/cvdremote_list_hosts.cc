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

#include <iostream>

#include <android-base/logging.h>
#include <gflags/gflags.h>

#include "host/commands/remote/remote.h"
#include "host/libs/web/http_client/sso_client.h"

DEFINE_string(service_url, "", "cloud orchestration service url");
DEFINE_string(zone, "us-central1-b", "cloud zone");
DEFINE_bool(use_sso_client, false,
            "communicates with cloud orchestration using sso_client_binary");

namespace cuttlefish {
namespace {

int Main(int argc, char** argv) {
  gflags::ParseCommandLineFlags(&argc, &argv, true);
  if (FLAGS_service_url == "") {
    LOG(ERROR) << "Missing host-url flag";
    return -1;
  }
  auto http_client =
      FLAGS_use_sso_client
          ? std::unique_ptr<HttpClient>(new http_client::SsoClient())
          : HttpClient::CurlClient();
  CloudOrchestratorApi api(FLAGS_service_url, FLAGS_zone, *http_client);
  auto hosts = api.ListHosts();
  if (!hosts.ok()) {
    LOG(ERROR) << hosts.error().message();
    return -1;
  }
  for (auto host : *hosts) {
    std::cout << host << std::endl;
  }
  return 0;
}

}  // namespace
}  // namespace cuttlefish

int main(int argc, char** argv) { return cuttlefish::Main(argc, argv); }
