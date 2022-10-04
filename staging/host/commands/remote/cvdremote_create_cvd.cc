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

DEFINE_string(service_url, "", "Cloud orchestration service url.");
DEFINE_string(zone, "us-central1-b", "Cloud zone.");
DEFINE_string(host, "", "If empty, a new host will be created.");
DEFINE_bool(use_sso_client, false,
            "Communicates with cloud orchestration using sso_client_binary");
DEFINE_string(build_id, "", "Android build identifier.");
DEFINE_string(target, "aosp_cf_x86_64_phone-userdebug",
              "Android build target.");

namespace cuttlefish {
namespace {

int Main(int argc, char** argv) {
  gflags::ParseCommandLineFlags(&argc, &argv, true);
  if (FLAGS_service_url == "") {
    LOG(ERROR) << "Missing host-url flag";
    return -1;
  }
  if (FLAGS_host == "") {
    LOG(ERROR)
        << "Creating a cvd instance without a host is not implemented yet.";
    return -1;
  }
  if (FLAGS_build_id == "") {
    LOG(ERROR) << "Missing --build_id flag.";
    return -1;
  }
  auto http_client =
      FLAGS_use_sso_client
          ? std::unique_ptr<HttpClient>(new http_client::SsoClient())
          : HttpClient::CurlClient();
  CloudOrchestratorApi api(FLAGS_service_url, FLAGS_zone, *http_client);
  CreateCVDRequest request{
    build_info : BuildInfo{
      build_id : FLAGS_build_id,
      target : FLAGS_target,
    },
  };
  auto result = api.CreateCVD(FLAGS_host, request);
  if (!result.ok()) {
    LOG(ERROR) << result.error().Message();
    return -1;
  }
  std::cout << *result << std::endl;
  return 0;
}

}  // namespace
}  // namespace cuttlefish

int main(int argc, char** argv) { return cuttlefish::Main(argc, argv); }
