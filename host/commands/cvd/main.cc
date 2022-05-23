/*
 * Copyright (C) 2021 The Android Open Source Project
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

#include <stdlib.h>
#include <chrono>
#include <iostream>
#include <map>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include <android-base/file.h>
#include <android-base/logging.h>
#include <android-base/result.h>
#include <build/version.h>

#include "cvd_server.pb.h"

#include "common/libs/fs/shared_fd.h"
#include "common/libs/utils/environment.h"
#include "common/libs/utils/files.h"
#include "common/libs/utils/flag_parser.h"
#include "common/libs/utils/result.h"
#include "common/libs/utils/shared_fd_flag.h"
#include "common/libs/utils/subprocess.h"
#include "common/libs/utils/unix_sockets.h"
#include "host/commands/cvd/fetch_cvd.h"
#include "host/commands/cvd/server.h"
#include "host/commands/cvd/server_constants.h"
#include "host/libs/config/cuttlefish_config.h"
#include "host/libs/config/host_tools_version.h"

namespace cuttlefish {
namespace {

Result<SharedFD> ConnectToServer() {
  auto connection =
      SharedFD::SocketLocalClient(cvd::kServerSocketPath,
                                  /*is_abstract=*/true, SOCK_SEQPACKET);
  if (!connection->IsOpen()) {
    auto connection =
        SharedFD::SocketLocalClient(cvd::kServerSocketPath,
                                    /*is_abstract=*/true, SOCK_STREAM);
  }
  if (!connection->IsOpen()) {
    return CF_ERR("Failed to connect to server" << connection->StrError());
  }
  return connection;
}

class CvdClient {
 public:
  Result<void> EnsureCvdServerRunning(const std::string& host_tool_directory,
                                      int num_retries = 1) {
    cvd::Request request;
    request.mutable_version_request();
    auto response = SendRequest(request);

    // If cvd_server is not running, start and wait before checking its version.
    if (!response.ok()) {
      CF_EXPECT(StartCvdServer(host_tool_directory));
      response = CF_EXPECT(SendRequest(request));
    }
    CF_EXPECT(CheckStatus(response->status(), "GetVersion"));
    CF_EXPECT(response->has_version_response(),
              "GetVersion call missing VersionResponse.");

    auto server_version = response->version_response().version();
    if (server_version.major() != cvd::kVersionMajor) {
      return CF_ERR("Major version difference: cvd("
                    << cvd::kVersionMajor << "." << cvd::kVersionMinor
                    << ") != cvd_server(" << server_version.major() << "."
                    << server_version.minor()
                    << "). Try `cvd kill-server` or `pkill cvd_server`.");
    }
    if (server_version.minor() < cvd::kVersionMinor) {
      std::cerr << "Minor version of cvd_server is older than latest. "
                << "Attempting to restart..." << std::endl;
      CF_EXPECT(StopCvdServer(/*clear=*/false));
      CF_EXPECT(StartCvdServer(host_tool_directory));
      if (num_retries > 0) {
        CF_EXPECT(EnsureCvdServerRunning(host_tool_directory, num_retries - 1));
        return {};
      } else {
        return CF_ERR("Unable to start the cvd_server with version "
                      << cvd::kVersionMajor << "." << cvd::kVersionMinor);
      }
    }
    if (server_version.build() != android::build::GetBuildNumber()) {
      LOG(VERBOSE) << "cvd_server client version ("
                   << android::build::GetBuildNumber()
                   << ") does not match server version ("
                   << server_version.build() << std::endl;
    }
    auto self_crc32 = FileCrc("/proc/self/exe");
    if (server_version.crc32() != self_crc32) {
      LOG(VERBOSE) << "cvd_server client checksum (" << self_crc32
                   << ") doesn't match server checksum ("
                   << server_version.crc32() << std::endl;
    }
    return {};
  }

  Result<void> StopCvdServer(bool clear) {
    if (!server_) {
      // server_ may not represent a valid connection even while the server is
      // running, if we haven't tried to connect. This establishes first whether
      // the server is running.
      auto connection_attempt = ConnectToServer();
      if (!connection_attempt.ok()) {
        return {};
      }
    }

    cvd::Request request;
    auto shutdown_request = request.mutable_shutdown_request();
    if (clear) {
      shutdown_request->set_clear(true);
    }

    // Send the server a pipe with the Shutdown request that it
    // will close when it fully exits.
    SharedFD read_pipe, write_pipe;
    CF_EXPECT(cuttlefish::SharedFD::Pipe(&read_pipe, &write_pipe),
              "Unable to create shutdown pipe: " << strerror(errno));

    auto response = SendRequest(request, /*extra_fd=*/write_pipe);

    // If the server is already not running then SendRequest will fail.
    // We treat this as success.
    if (!response.ok()) {
      server_.reset();
      return {};
    }

    CF_EXPECT(CheckStatus(response->status(), "Shutdown"));
    CF_EXPECT(response->has_shutdown_response(),
              "Shutdown call missing ShutdownResponse.");

    // Clear out the server_ socket.
    server_.reset();

    // Close the write end of the pipe in this process. Now the only
    // process that may have the write end still open is the cvd_server.
    write_pipe->Close();

    // Wait for the pipe to close by attempting to read from the pipe.
    char buf[1];  // Any size >0 should work for read attempt.
    CF_EXPECT(read_pipe->Read(buf, sizeof(buf)) <= 0,
              "Unexpected read value from cvd_server shutdown pipe.");
    return {};
  }

  Result<void> HandleCommand(std::vector<std::string> args,
                             std::vector<std::string> env) {
    cvd::Request request;
    auto command_request = request.mutable_command_request();
    for (const std::string& arg : args) {
      command_request->add_args(arg);
    }
    for (const std::string& e : env) {
      auto eq_pos = e.find('=');
      if (eq_pos == std::string::npos) {
        LOG(WARNING) << "Environment var in unknown format: " << e;
        continue;
      }
      (*command_request->mutable_env())[e.substr(0, eq_pos)] =
          e.substr(eq_pos + 1);
    }
    std::unique_ptr<char, void(*)(void*)> cwd(getcwd(nullptr, 0), &free);
    command_request->set_working_directory(cwd.get());
    command_request->set_wait_behavior(cvd::WAIT_BEHAVIOR_COMPLETE);

    std::optional<SharedFD> exe_fd;
    if (args.size() > 2 && android::base::Basename(args[0]) == "cvd" &&
        args[1] == "restart-server" && args[2] == "match-client") {
      constexpr char kSelf[] = "/proc/self/exe";
      exe_fd = SharedFD::Open(kSelf, O_RDONLY);
      CF_EXPECT((*exe_fd)->IsOpen(), "Failed to open \""
                                         << kSelf << "\": \""
                                         << (*exe_fd)->StrError() << "\"");
    }
    auto response = CF_EXPECT(SendRequest(request, exe_fd));
    CF_EXPECT(CheckStatus(response.status(), "HandleCommand"));
    CF_EXPECT(response.has_command_response(),
              "HandleCommand call missing CommandResponse.");
    return {};
  }

 private:
  std::optional<UnixMessageSocket> server_;

  Result<void> SetServer(const SharedFD& server) {
    CF_EXPECT(!server_, "Already have a server");
    CF_EXPECT(server->IsOpen(), server->StrError());
    server_ = UnixMessageSocket(server);
    CF_EXPECT(server_->EnableCredentials(true).ok(),
              "Unable to enable UnixMessageSocket credentials.");
    return {};
  }

  Result<cvd::Response> SendRequest(const cvd::Request& request,
                                    std::optional<SharedFD> extra_fd = {}) {
    if (!server_) {
      CF_EXPECT(SetServer(CF_EXPECT(ConnectToServer())));
    }
    // Serialize and send the request.
    std::string serialized;
    CF_EXPECT(request.SerializeToString(&serialized),
              "Unable to serialize request proto.");
    UnixSocketMessage request_message;

    std::vector<SharedFD> control_fds = {
        SharedFD::Dup(0),
        SharedFD::Dup(1),
        SharedFD::Dup(2),
    };
    if (extra_fd) {
      control_fds.push_back(*extra_fd);
    }
    auto control = CF_EXPECT(ControlMessage::FromFileDescriptors(control_fds));
    request_message.control.emplace_back(std::move(control));

    request_message.data =
        std::vector<char>(serialized.begin(), serialized.end());
    CF_EXPECT(server_->WriteMessage(request_message));

    // Read and parse the response.
    auto read_result = CF_EXPECT(server_->ReadMessage());
    serialized = std::string(read_result.data.begin(), read_result.data.end());
    cvd::Response response;
    CF_EXPECT(response.ParseFromString(serialized),
              "Unable to parse serialized response proto.");
    return response;
  }

  Result<void> StartCvdServer(const std::string& host_tool_directory) {
    SharedFD server_fd =
        SharedFD::SocketLocalServer(cvd::kServerSocketPath,
                                    /*is_abstract=*/true, SOCK_SEQPACKET, 0666);
    CF_EXPECT(server_fd->IsOpen(), server_fd->StrError());

    // TODO(b/196114111): Investigate fully "daemonizing" the cvd_server.
    CF_EXPECT(setenv("ANDROID_HOST_OUT", host_tool_directory.c_str(),
                     /*overwrite=*/true) == 0);
    Command command("/proc/self/exe");
    command.AddParameter("-INTERNAL_server_fd=", server_fd);
    SubprocessOptions options;
    options.ExitWithParent(false);
    command.Start(options);

    // Connect to the server_fd, which waits for startup.
    CF_EXPECT(SetServer(SharedFD::SocketLocalClient(cvd::kServerSocketPath,
                                                    /*is_abstract=*/true,
                                                    SOCK_SEQPACKET)));
    return {};
  }

  Result<void> CheckStatus(const cvd::Status& status, const std::string& rpc) {
    if (status.code() == cvd::Status::OK) {
      return {};
    }
    return CF_ERR("Received error response for \"" << rpc << "\":\n"
                                                   << status.message()
                                                   << "\nIn client");
  }
};

[[noreturn]] void CallPythonAcloud(std::vector<std::string>& args) {
  auto android_top = StringFromEnv("ANDROID_BUILD_TOP", "");
  if (android_top == "") {
    LOG(FATAL) << "Could not find android environment. Please run "
               << "\"source build/envsetup.sh\".";
    abort();
  }
  // TODO(b/206893146): Detect what the platform actually is.
  auto py_acloud_path =
      android_top + "/prebuilts/asuite/acloud/linux-x86/acloud";
  char** new_argv = new char*[args.size() + 1];
  for (size_t i = 0; i < args.size(); i++) {
    new_argv[i] = args[i].data();
  }
  new_argv[args.size()] = nullptr;
  execv(py_acloud_path.data(), new_argv);
  PLOG(FATAL) << "execv(" << py_acloud_path << ", ...) failed";
  abort();
}

Result<void> CvdMain(int argc, char** argv, char** envp) {
  android::base::InitLogging(argv, android::base::StderrLogger);

  std::vector<std::string> args = ArgsToVec(argc, argv);
  std::vector<Flag> flags;

  CvdClient client;

  // TODO(b/206893146): Make this decision inside the server.
  if (android::base::Basename(args[0]) == "acloud") {
    auto server_running = client.EnsureCvdServerRunning(
        android::base::Dirname(android::base::GetExecutableDirectory()));
    if (server_running.ok()) {
      // TODO(schuffelen): Deduplicate when calls to setenv are removed.
      std::vector<std::string> env;
      for (char** e = envp; *e != 0; e++) {
        env.emplace_back(*e);
      }
      args[0] = "try-acloud";
      auto attempt = client.HandleCommand(args, env);
      if (attempt.ok()) {
        args[0] = "acloud";
        CF_EXPECT(client.HandleCommand(args, env));
        return {};
      } else {
        CallPythonAcloud(args);
      }
    } else {
      // Something is wrong with the server, fall back to python acloud
      CallPythonAcloud(args);
    }
  } else if (android::base::Basename(args[0]) == "fetch_cvd") {
    CF_EXPECT(FetchCvdMain(argc, argv));
    return {};
  }
  bool clean = false;
  flags.emplace_back(GflagsCompatFlag("clean", clean));
  SharedFD internal_server_fd;
  flags.emplace_back(SharedFDFlag("INTERNAL_server_fd", internal_server_fd));
  SharedFD carryover_client_fd;
  flags.emplace_back(
      SharedFDFlag("INTERNAL_carryover_client_fd", carryover_client_fd));

  CF_EXPECT(ParseFlags(flags, args));

  if (internal_server_fd->IsOpen()) {
    CF_EXPECT(CvdServerMain(internal_server_fd, carryover_client_fd));
    return {};
  } else if (argv[0] == std::string("/proc/self/exe")) {
    return CF_ERR(
        "Expected to be in server mode, but didn't get a server "
        "fd: "
        << internal_server_fd->StrError());
  }

  // Special case for `cvd kill-server`, handled by directly
  // stopping the cvd_server.
  if (argc > 1 && strcmp("kill-server", argv[1]) == 0) {
    CF_EXPECT(client.StopCvdServer(/*clear=*/true));
    return {};
  }

  // Special case for --clean flag, used to clear any existing state.
  if (clean) {
    LOG(INFO) << "cvd invoked with --clean; "
              << "stopping the cvd_server before continuing.";
    CF_EXPECT(client.StopCvdServer(/*clear=*/true));
  }

  // Handle all remaining commands by forwarding them to the cvd_server.
  CF_EXPECT(client.EnsureCvdServerRunning(android::base::Dirname(
                android::base::GetExecutableDirectory())),
            "Unable to ensure cvd_server is running.");

  // TODO(schuffelen): Deduplicate when calls to setenv are removed.
  std::vector<std::string> env;
  for (char** e = envp; *e != 0; e++) {
    env.emplace_back(*e);
  }
  CF_EXPECT(client.HandleCommand(args, env));
  return {};
}

}  // namespace
}  // namespace cuttlefish

int main(int argc, char** argv, char** envp) {
  auto result = cuttlefish::CvdMain(argc, argv, envp);
  if (result.ok()) {
    return 0;
  } else {
    std::cerr << result.error() << std::endl;
    return -1;
  }
}
