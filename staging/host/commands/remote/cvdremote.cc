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

#include "host/commands/remote/actions.h"
#include "host/commands/remote/output.h"
#include "host/commands/remote/remote.h"
#include "host/libs/web/http_client/sso_client.h"

const std::string kUsage = R"(
NAME
    cvdremote - manage Cuttlefish Virtual Devices (CVDs) in the cloud.

SYNOPSIS
    cvdremote --service_url=<url> --zone=<zone> [<resource>] <command> [<args>]

RESOURCES
    cvd (default)
        Cuttlefish Virtual Devices.

    host
        Host machines where CVDs live.

COMMANDS
    create
        Create a resource.

    list
        List the resources.
)";

// General flags.
DEFINE_string(service_url, "", "Cloud orchestration service url.");
DEFINE_string(zone, "us-central1-b", "Cloud zone.");
DEFINE_bool(verbose, false,
            "Indicates whether to print a verbose output or not.");
DEFINE_bool(use_sso_client, false,
            "Communicates with cloud orchestration using sso_client_binary");

// Flags specifics to host resource commands.
DEFINE_string(machine_type, "zones/us-central1-b/machineTypes/n1-standard-4",
              "Full or partial URL of the machine type resource.");
DEFINE_string(min_cpu_platform, "Intel Haswell",
              "Specifies a minimum CPU platform for the VM instance.");

// Flags specifics to cvd resource commands.
DEFINE_string(host, "", "If empty, cvds from all hosts will be printed out.");
DEFINE_string(build_id, "", "Android build identifier.");
DEFINE_string(target, "aosp_cf_x86_64_phone-userdebug",
              "Android build target.");

namespace cuttlefish {
namespace {

//
// Create host.
//
int CommandCreateHostMain(const std::vector<std::string>&) {
  auto http_client =
      FLAGS_use_sso_client
          ? std::unique_ptr<HttpClient>(new http_client::SsoClient())
          : HttpClient::CurlClient();
  CloudOrchestratorApi api(FLAGS_service_url, FLAGS_zone, *http_client);
  GCPInstance gcp;
  gcp.machine_type = FLAGS_machine_type.c_str();
  gcp.min_cpu_platform = FLAGS_min_cpu_platform.c_str();
  CreateHostInstanceRequest request;
  request.gcp = &gcp;
  auto action = CreateHostAction(api, request);
  auto result = action->Execute();
  if (!result.ok()) {
    std::cerr << result.error().Message();
    return -1;
  }
  std::cout << *result << std::endl;
  return 0;
}

//
// List hosts.
//
int CommandListHostsMain(const std::vector<std::string>&) {
  auto http_client =
      FLAGS_use_sso_client
          ? std::unique_ptr<HttpClient>(new http_client::SsoClient())
          : HttpClient::CurlClient();
  CloudOrchestratorApi api(FLAGS_service_url, FLAGS_zone, *http_client);
  auto hosts = api.ListHosts();
  if (!hosts.ok()) {
    std::cerr << hosts.error().Message();
    LOG(DEBUG) << hosts.error().Trace();
    return -1;
  }
  if ((*hosts).empty()) {
    std::cerr << "~ No hosts found ~" << std::endl;
    return 0;
  }
  for (const std::string& host : *hosts) {
    std::cout << host << std::endl;
  }
  return 0;
}

//
// Delete host.
//
int CommandDeleteHostMain(const std::vector<std::string>& args) {
  if (args.empty()) {
    std::cerr << "Missing host name." << std::endl;
    return -1;
  }
  auto http_client =
      FLAGS_use_sso_client
          ? std::unique_ptr<HttpClient>(new http_client::SsoClient())
          : HttpClient::CurlClient();
  CloudOrchestratorApi api(FLAGS_service_url, FLAGS_zone, *http_client);
  auto action = DeleteHostsAction(api, args);
  auto action_result = action->Execute();
  if (!action_result.ok()) {
    std::cerr << action_result.error().Message();
    return -1;
  }
  bool any_del_had_error = false;
  for (auto& del_instance_result : *action_result) {
    if (!del_instance_result.ok()) {
      std::cerr << del_instance_result.error().Message() << std::endl
                << std::endl;
      any_del_had_error = true;
    }
  }
  if (any_del_had_error) {
    return -1;
  }
  return 0;
}

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

//
// Create cvd.
//
int CommandCreateCVDMain(const std::vector<std::string>&) {
  if (FLAGS_build_id == "") {
    std::cerr << "Missing --build_id flag.";
    return -1;
  }
  auto http_client =
      FLAGS_use_sso_client
          ? std::unique_ptr<HttpClient>(new http_client::SsoClient())
          : HttpClient::CurlClient();
  auto retrying_http_client = HttpClient::ServerErrorRetryClient(
      *http_client, 5 /* retry_attempts */,
      std::chrono::milliseconds(5000) /* retry_delay */);
  CloudOrchestratorApi api(FLAGS_service_url, FLAGS_zone,
                           *retrying_http_client);
  std::string host = FLAGS_host;
  if (host == "") {
    GCPInstance gcp;
    gcp.machine_type = FLAGS_machine_type.c_str();
    gcp.min_cpu_platform = FLAGS_min_cpu_platform.c_str();
    CreateHostInstanceRequest request;
    request.gcp = &gcp;
    auto action = CreateHostAction(api, request);
    auto result = action->Execute();
    if (!result.ok()) {
      std::cerr << result.error().Message();
      return -1;
    }
    host = *result;
  }
  CreateCVDRequest request{
    build_info : BuildInfo{
      build_id : FLAGS_build_id,
      target : FLAGS_target,
    },
  };
  auto action = CreateCVDAction(api, request, host);
  auto result = action->Execute();
  if (!result.ok()) {
    std::cerr << result.error().Message();
    return -1;
  }
  std::cout << *result << std::endl;
  return 0;
}

// List cvds.
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

int CommandListCVDsMain(const std::vector<std::string>&) {
  auto http_client =
      FLAGS_use_sso_client
          ? std::unique_ptr<HttpClient>(new http_client::SsoClient())
          : HttpClient::CurlClient();
  CloudOrchestratorApi api(FLAGS_service_url, FLAGS_zone, *http_client);
  // TODO(b/248087309): Implements list cvds with multiple hosts asynchronously.
  if (FLAGS_host == "") {
    auto hosts = api.ListHosts();
    if (!hosts.ok()) {
      std::cerr << hosts.error().Message();
      LOG(DEBUG) << hosts.error().Trace();
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
      std::cerr << cvd_streams.error().Message();
      LOG(DEBUG) << cvd_streams.error().Trace();
      return -1;
    }
    PrintCVDs(FLAGS_host, *cvd_streams);
  }
  return 0;
}

constexpr char kResourceHost[] = "host";
constexpr char kResourceCVD[] = "cvd";

constexpr char kCommandList[] = "list";
constexpr char kCommandCreate[] = "create";
constexpr char kCommandDelete[] = "delete";

std::map<
    std::string,
    std::map<std::string, std::function<int(const std::vector<std::string>&)>>>
    commands_map = {
        {kResourceHost,
         {
             {kCommandCreate, CommandCreateHostMain},
             {kCommandList, CommandListHostsMain},
             {kCommandDelete, CommandDeleteHostMain},
         }},
        {kResourceCVD,
         {
             {kCommandCreate, CommandCreateCVDMain},
             {kCommandList, CommandListCVDsMain},
         }},
};

int Main(int argc, char** argv) {
  ::gflags::SetUsageMessage(kUsage);
  ::gflags::ParseCommandLineFlags(&argc, &argv, true);
  ::android::base::InitLogging(argv, android::base::StderrLogger);
  if (FLAGS_service_url == "") {
    std::cerr << "Missing service_url flag";
    return -1;
  }
  std::vector<std::string> args;
  for (int i = 1; i < argc; i++) {
    args.push_back(argv[i]);
  }
  if (args.empty()) {
    std::cerr << "Missing command";
    return -1;
  }
  if (args.size() == 1) {
    args.insert(args.begin(), kResourceCVD);
  }
  std::string resource = args[0];
  args.erase(args.begin());
  if (commands_map.find(resource) == commands_map.end()) {
    std::cerr << "Invalid resource"
              << " \"" << resource << "\".";
    return -1;
  }
  std::string command = args[0];
  args.erase(args.begin());
  const auto& res_commands_map = commands_map[resource];
  if (res_commands_map.find(command) == res_commands_map.end()) {
    std::cerr << "Invalid command"
              << " \"" << command << "\" "
              << "for"
              << " \"" << resource << "\" resource.";
    return -1;
  }
  return commands_map[resource][command](args);
}

}  // namespace
}  // namespace cuttlefish

int main(int argc, char** argv) { return cuttlefish::Main(argc, argv); }
