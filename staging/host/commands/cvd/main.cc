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

#include "cvd_server.pb.h"

#include "common/libs/fs/shared_fd.h"
#include "common/libs/utils/environment.h"
#include "common/libs/utils/files.h"
#include "common/libs/utils/flag_parser.h"
#include "common/libs/utils/subprocess.h"
#include "common/libs/utils/unix_sockets.h"
#include "host/commands/cvd/server.h"
#include "host/libs/config/cuttlefish_config.h"

namespace cuttlefish {
namespace {

class CvdClient {
 public:
  CvdClient(const SharedFD& server) { SetServer(server); }

  void SetServer(const SharedFD& server) {
    CHECK(server->IsOpen()) << "Unable to open connection to cvd_server.";
    server_ = UnixMessageSocket(server);
    CHECK(server_->EnableCredentials(true).ok())
        << "Unable to enable UnixMessageSocket credentials.";
  }

  bool EnsureCvdServerRunning(const std::string& host_tool_directory,
                              int num_retries = 1) {
    cvd::Request request;
    request.mutable_version_request();
    auto response = SendRequest(request);

    // If cvd_server is not running, start and wait before checking its version.
    if (!response.ok()) {
      StartCvdServer(host_tool_directory);
      response = SendRequest(request);
    }
    CHECK(response.ok() && response->has_version_response())
        << "GetVersion call missing VersionResponse.";
    CheckStatus(response->status(), "GetVersion");

    auto server_version = response->version_response().version();
    if (server_version.major() != cvd::kVersionMajor) {
      std::cout << "Major version difference: cvd(" << cvd::kVersionMajor << "."
                << cvd::kVersionMinor << ") != cvd_server("
                << server_version.major() << "." << server_version.minor()
                << "). Try `cvd kill-server` or `pkill cvd_server`."
                << std::endl;
      return false;
    }
    if (server_version.minor() < cvd::kVersionMinor) {
      std::cout << "Minor version of cvd_server is older than latest. "
                << "Attempting to restart..." << std::endl;
      StopCvdServer(/*clear=*/false);
      StartCvdServer(host_tool_directory);
      if (num_retries > 0) {
        return EnsureCvdServerRunning(host_tool_directory, num_retries - 1);
      } else {
        std::cout << "Unable to start the cvd_server with version "
                  << cvd::kVersionMajor << "." << cvd::kVersionMinor;
        return false;
      }
    }
    return true;
  }

  void StopCvdServer(bool clear) {
    if (!server_) {
      return;
    }

    cvd::Request request;
    auto shutdown_request = request.mutable_shutdown_request();
    if (clear) {
      shutdown_request->set_clear(true);
    }

    // Send the server a pipe with the Shutdown request that it
    // will close when it fully exits.
    SharedFD read_pipe, write_pipe;
    CHECK(cuttlefish::SharedFD::Pipe(&read_pipe, &write_pipe))
        << "Unable to create shutdown pipe: " << strerror(errno);

    auto response = SendRequest(request, /*extra_fd=*/write_pipe);

    // If the server is already not running then SendRequest will fail.
    // We treat this as success.
    if (!response.ok()) {
      server_.reset();
      return;
    }

    CHECK(response->has_shutdown_response())
        << "Shutdown call missing ShutdownResponse.";
    CheckStatus(response->status(), "Shutdown");

    // Clear out the server_ socket.
    server_.reset();

    // Close the write end of the pipe in this process. Now the only
    // process that may have the write end still open is the cvd_server.
    write_pipe->Close();

    // Wait for the pipe to close by attempting to read from the pipe.
    char buf[1];  // Any size >0 should work for read attempt.
    CHECK(read_pipe->Read(buf, sizeof(buf)) <= 0)
        << "Unexpected read value from cvd_server shutdown pipe.";
  }

  void HandleCommand(std::vector<std::string> args,
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

    auto response = SendRequest(request);
    CHECK(response.ok() && response->has_command_response())
        << "HandleCommand call missing CommandResponse.";
    CheckStatus(response->status(), "GetVersion");
  }

 private:
  std::optional<UnixMessageSocket> server_;

  android::base::Result<cvd::Response> SendRequest(
      const cvd::Request& request, std::optional<SharedFD> extra_fd = {}) {
    if (!server_) {
      return android::base::Error() << "server_ not set, cannot SendRequest.";
    }
    // Serialize and send the request.
    std::string serialized;
    if (!request.SerializeToString(&serialized)) {
      return android::base::Error() << "Unable to serialize request proto.";
    }
    UnixSocketMessage request_message;

    std::vector<SharedFD> control_fds = {
        SharedFD::Dup(0),
        SharedFD::Dup(1),
        SharedFD::Dup(2),
    };
    if (extra_fd) {
      control_fds.push_back(*extra_fd);
    }
    auto control = ControlMessage::FromFileDescriptors(control_fds);
    CHECK(control.ok()) << control.error();
    request_message.control.emplace_back(std::move(*control));

    request_message.data =
        std::vector<char>(serialized.begin(), serialized.end());
    auto write_result = server_->WriteMessage(request_message);
    if (!write_result.ok()) {
      return android::base::Error() << write_result.error();
    }

    // Read and parse the response.
    auto read_result = server_->ReadMessage();
    if (!read_result.ok()) {
      return android::base::Error() << read_result.error();
    }
    serialized =
        std::string(read_result->data.begin(), read_result->data.end());
    cvd::Response response;
    if (!response.ParseFromString(serialized)) {
      return android::base::Error()
             << "Unable to parse serialized response proto.";
    }

    return response;
  }

  void StartCvdServer(const std::string& host_tool_directory) {
    SharedFD server_fd =
        SharedFD::SocketLocalServer(cvd::kServerSocketPath,
                                    /*is_abstract=*/true, SOCK_STREAM, 0666);
    CHECK(server_fd->IsOpen()) << server_fd->StrError();

    // TODO(b/196114111): Investigate fully "daemonizing" the cvd_server.
    CHECK(setenv("ANDROID_HOST_OUT", host_tool_directory.c_str(),
                 /*overwrite=*/true) == 0);
    Command command(HostBinaryPath("cvd_server"));
    command.AddParameter("-server_fd=", server_fd);
    SubprocessOptions options;
    options.ExitWithParent(false);
    command.Start(options);

    // Connect to the server_fd, which waits for startup.
    SetServer(SharedFD::SocketLocalClient(cvd::kServerSocketPath,
                                          /*is_abstract=*/true, SOCK_STREAM));
  }

  void CheckStatus(const cvd::Status& status, const std::string& rpc) {
    CHECK(status.code() == cvd::Status::OK)
        << "Failed to call cvd_server " << rpc << " (" << status.code()
        << "): " << status.message();
  }
};

int CvdMain(int argc, char** argv, char** envp) {
  android::base::InitLogging(argv, android::base::StderrLogger);

  std::vector<std::string> args = ArgsToVec(argc, argv);
  std::vector<Flag> flags;

  // TODO(b/206893146): Make this decision inside the server.
  if (args[0] == "acloud") {
    bool passthrough = true;
    ParseFlags({GflagsCompatFlag("acloud_passthrough", passthrough)}, args);
    if (passthrough) {
      auto android_top = StringFromEnv("ANDROID_BUILD_TOP", "");
      if (android_top == "") {
        LOG(ERROR) << "Could not find android environment. Please run "
                   << "\"source build/envsetup.sh\".";
        return 1;
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
      delete[] new_argv;
      PLOG(ERROR) << "execv(" << py_acloud_path << ", ...) failed";
      return 1;
    }
  }
  bool clean = false;
  flags.emplace_back(GflagsCompatFlag("clean", clean));

  CHECK(ParseFlags(flags, args));

  CvdClient client(SharedFD::SocketLocalClient(cvd::kServerSocketPath,
                                               /*is_abstract=*/true,
                                               SOCK_STREAM));

  // Special case for `cvd kill-server`, handled by directly
  // stopping the cvd_server.
  if (argc > 1 && strcmp("kill-server", argv[1]) == 0) {
    client.StopCvdServer(/*clear=*/true);
    return 0;
  }

  // Special case for --clean flag, used to clear any existing state.
  if (clean) {
    LOG(INFO) << "cvd invoked with --clean; "
              << "stopping the cvd_server before continuing.";
    client.StopCvdServer(/*clear=*/true);
    client = CvdClient(SharedFD::SocketLocalClient(
        cvd::kServerSocketPath, /*is_abstract=*/true, SOCK_STREAM));
  }

  // Handle all remaining commands by forwarding them to the cvd_server.
  CHECK(client.EnsureCvdServerRunning(
      android::base::Dirname(android::base::GetExecutableDirectory())))
      << "Unable to ensure cvd_server is running.";

  std::vector<std::string> env;
  for (char** e = envp; *e != 0; e++) {
    env.emplace_back(*e);
  }
  client.HandleCommand(args, env);
  return 0;
}

}  // namespace
}  // namespace cuttlefish

int main(int argc, char** argv, char** envp) {
  return cuttlefish::CvdMain(argc, argv, envp);
}
