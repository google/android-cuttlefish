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

// List remote cvds.
//
// Non-verbose output:
//   Format: "[INSTANCE_NAME] ([HOST_IDENTIFIER])"
//   Example:
//     cvd-1 (cf-ec559de7-6621-4ace-a8be-0f480a6f9498)
//     cvd-2 (cf-ec559de7-6621-4ace-a8be-0f480a6f9498)
//     cvd-3 (cf-ec559de7-6621-4ace-a8be-0f480a6f9498)
//     cvd-1 (cf-e4b0b61d-21c4-497e-8045-bd48c37e487e)
//     cvd-1 (cf-b3aa26b2-1312-4241-989f-b80f92d6d9ae)
//
// Verbose output:
//   Format:
//     ```
//     [INSTANCE_NAME] ([HOST_IDENTIFIER])
//       [KEY_1]: [VALUE_1]
//       [KEY_2]: [VALUE_3]
//       ...
//       [KEY_N]: [VALUE_N]
//
//     ```
//   Example:
//     [1] cvd-1 (cf-ec559de7-6621-4ace-a8be-0f480a6f9498)
//           create time: 2018-10-25T06:32:08.182-07:00
//           display: 1080x1920 (240)
//           webrtcstream_url: https://foo.com/.../client.html
//
//     [1] cvd-2 (cf-ec559de7-6621-4ace-a8be-0f480a6f9498)
//           create time: 2018-10-25T06:32:08.182-07:00
//           display: 1080x1920 (240)
//           webrtcstream_url: https://foo.com/.../client.html

#include <iostream>

#include <android-base/logging.h>
#include <gflags/gflags.h>

#include "host/commands/remote/output.h"
#include "host/commands/remote/remote.h"
#include "host/libs/web/http_client/sso_client.h"

DEFINE_string(service_url, "", "Cloud orchestration service url.");
DEFINE_string(zone, "us-central1-b", "Cloud zone.");
DEFINE_string(host, "", "If empty, cvds from all hosts will be printed out.");
DEFINE_bool(verbose, false,
            "Indicates whether to print a verbose output or not.");
DEFINE_bool(use_sso_client, false,
            "Communicates with cloud orchestration using sso_client_binary");

namespace cuttlefish {
namespace {

void PrintCVDs(const std::string& host, const std::vector<std::string>& cvds) {
  for (const std::string& cvd : cvds) {
    CVDOutput o{
      service_url : FLAGS_service_url,
      zone : FLAGS_zone,
      host : host,
      verbose : FLAGS_verbose,
      name : cvd
    };
    std::cout << o.ToString() << std::endl;
  }
}

int Main(int argc, char** argv) {
  gflags::ParseCommandLineFlags(&argc, &argv, true);
  if (FLAGS_service_url == "") {
    LOG(ERROR) << "Missing service url flag";
    return -1;
  }
  auto http_client =
      FLAGS_use_sso_client
          ? std::unique_ptr<HttpClient>(new http_client::SsoClient())
          : HttpClient::CurlClient();
  CloudOrchestratorApi api(FLAGS_service_url, FLAGS_zone, *http_client);
  // TODO(b/248087309): Implements list cvds with multiple hosts asynchronously.
  if (FLAGS_host == "") {
    auto hosts = api.ListHosts();
    if (!hosts.ok()) {
      LOG(ERROR) << hosts.error().message();
      return -1;
    }
    if ((*hosts).empty()) {
      std::cerr << "~ No cvds found ~" << std::endl;
      return 0;
    }
    for (const std::string& host : *hosts) {
      auto cvd_streams = api.ListCVDWebRTCStreams(host);
      if (!cvd_streams.ok()) {
        continue;
      }
      PrintCVDs(host, *cvd_streams);
    }
  } else {
    auto cvd_streams = api.ListCVDWebRTCStreams(FLAGS_host);
    if (!cvd_streams.ok()) {
      LOG(ERROR) << cvd_streams.error().message();
      return -1;
    }
    PrintCVDs(FLAGS_host, *cvd_streams);
  }
  return 0;
}

}  // namespace
}  // namespace cuttlefish

int main(int argc, char** argv) { return cuttlefish::Main(argc, argv); }
