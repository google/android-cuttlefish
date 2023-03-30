/*
 * Copyright (C) 2022 The Android Open Source Project
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

#include "client.h"

#include <stdlib.h>

#include <iostream>
#include <sstream>

#include <android-base/file.h>
#include <google/protobuf/text_format.h>

#include "common/libs/fs/shared_buf.h"
#include "common/libs/fs/shared_fd.h"
#include "common/libs/utils/environment.h"
#include "common/libs/utils/subprocess.h"
#include "host/commands/cvd/common_utils.h"
#include "host/commands/cvd/server_constants.h"
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
  std::unique_ptr<char*[]> new_argv(new char*[args.size() + 1]);
  for (size_t i = 0; i < args.size(); i++) {
    new_argv[i] = args[i].data();
  }
  new_argv[args.size()] = nullptr;
  execv(py_acloud_path.data(), new_argv.get());
  PLOG(FATAL) << "execv(" << py_acloud_path << ", ...) failed";
  abort();
}

}  // end of namespace

cvd::Version CvdClient::GetClientVersion() {
  cvd::Version client_version;
  client_version.set_major(cvd::kVersionMajor);
  client_version.set_minor(cvd::kVersionMinor);
  client_version.set_build(android::build::GetBuildNumber());
  client_version.set_crc32(FileCrc(kServerExecPath));
  return client_version;
}

Result<cvd::Version> CvdClient::GetServerVersion(
    const std::string& host_tool_directory) {
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

  return response->version_response().version();
}

Result<void> CvdClient::ValidateServerVersion(
    const std::string& host_tool_directory, int num_retries) {
  auto server_version = CF_EXPECT(GetServerVersion(host_tool_directory));
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
      CF_EXPECT(ValidateServerVersion(host_tool_directory, num_retries - 1));
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
  auto self_crc32 = FileCrc(kServerExecPath);
  if (server_version.crc32() != self_crc32) {
    LOG(VERBOSE) << "cvd_server client checksum (" << self_crc32
                 << ") doesn't match server checksum ("
                 << server_version.crc32() << std::endl;
  }
  return {};
}

Result<void> CvdClient::StopCvdServer(bool clear) {
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

  auto response =
      SendRequest(request, OverrideFd{/* override none of 0, 1, 2 */},
                  /*extra_fd=*/write_pipe);

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

Result<cvd::Response> CvdClient::HandleCommand(
    const std::vector<std::string>& args,
    const std::unordered_map<std::string, std::string>& env,
    const std::vector<std::string>& selector_args,
    const OverrideFd& new_control_fd) {
  std::optional<SharedFD> exe_fd;
  if (args.size() > 2 && android::base::Basename(args[0]) == "cvd" &&
      args[1] == "restart-server" && args[2] == "match-client") {
    exe_fd = SharedFD::Open(kServerExecPath, O_RDONLY);
    CF_EXPECT((*exe_fd)->IsOpen(), "Failed to open \""
                                       << kServerExecPath << "\": \""
                                       << (*exe_fd)->StrError() << "\"");
  }
  cvd::Request request = MakeRequest(
      {.cmd_args = args, .env = env, .selector_args = selector_args},
      cvd::WAIT_BEHAVIOR_COMPLETE, "");
  auto response = CF_EXPECT(SendRequest(request, new_control_fd, exe_fd));
  CF_EXPECT(CheckStatus(response.status(), "HandleCommand"));
  CF_EXPECT(response.has_command_response(),
            "HandleCommand call missing CommandResponse.");
  return {response};
}

Result<void> CvdClient::SetServer(const SharedFD& server) {
  CF_EXPECT(!server_, "Already have a server");
  CF_EXPECT(server->IsOpen(), server->StrError());
  server_ = UnixMessageSocket(server);
  CF_EXPECT(server_->EnableCredentials(true).ok(),
            "Unable to enable UnixMessageSocket credentials.");
  return {};
}

Result<cvd::Response> CvdClient::SendRequest(const cvd::Request& request,
                                             const OverrideFd& new_control_fds,
                                             std::optional<SharedFD> extra_fd) {
  if (!server_) {
    CF_EXPECT(SetServer(CF_EXPECT(ConnectToServer())));
  }
  // Serialize and send the request.
  std::string serialized;
  CF_EXPECT(request.SerializeToString(&serialized),
            "Unable to serialize request proto.");
  UnixSocketMessage request_message;

  std::vector<SharedFD> control_fds = {
      (new_control_fds.stdin_override_fd ? *new_control_fds.stdin_override_fd
                                         : SharedFD::Dup(0)),
      (new_control_fds.stdout_override_fd ? *new_control_fds.stdout_override_fd
                                          : SharedFD::Dup(1)),
      (new_control_fds.stderr_override_fd ? *new_control_fds.stderr_override_fd
                                          : SharedFD::Dup(2))};
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

Result<void> CvdClient::StartCvdServer(const std::string& host_tool_directory) {
  SharedFD server_fd =
      SharedFD::SocketLocalServer(cvd::kServerSocketPath,
                                  /*is_abstract=*/true, SOCK_SEQPACKET, 0666);
  CF_EXPECT(server_fd->IsOpen(), server_fd->StrError());

  // TODO(b/196114111): Investigate fully "daemonizing" the cvd_server.
  CF_EXPECT(setenv("ANDROID_HOST_OUT", host_tool_directory.c_str(),
                   /*overwrite=*/true) == 0);
  Command command(kServerExecPath);
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

Result<void> CvdClient::CheckStatus(const cvd::Status& status,
                                    const std::string& rpc) {
  if (status.code() == cvd::Status::OK) {
    return {};
  }
  return CF_ERR("Received error response for \"" << rpc << "\":\n"
                                                 << status.message()
                                                 << "\nIn client");
}

Result<void> CvdClient::HandleAcloud(
    const std::vector<std::string>& args,
    const std::unordered_map<std::string, std::string>& env,
    const std::string& host_tool_directory) {
  auto server_running =
      ValidateServerVersion(android::base::Dirname(host_tool_directory));

  std::vector<std::string> args_copy{args};

  // TODO(b/206893146): Make this decision inside the server.
  if (!server_running.ok()) {
    CallPythonAcloud(args_copy);
    // no return
  }

  args_copy[0] = "try-acloud";
  auto attempt = HandleCommand(args_copy, env, {});
  if (!attempt.ok()) {
    CallPythonAcloud(args_copy);
    // no return
  }

  args_copy[0] = "acloud";
  CF_EXPECT(HandleCommand(args_copy, env, {}));
  return {};
}

Result<std::string> CvdClient::HandleVersion(
    const std::string& host_tool_directory) {
  using google::protobuf::TextFormat;
  std::stringstream result;
  std::string output;
  auto server_version = CF_EXPECT(GetServerVersion(host_tool_directory));
  CF_EXPECT(TextFormat::PrintToString(server_version, &output),
            "converting server_version to string failed");
  result << "Server version:" << std::endl << std::endl << output << std::endl;

  CF_EXPECT(TextFormat::PrintToString(CvdClient::GetClientVersion(), &output),
            "converting client version to string failed");
  result << "Client version:" << std::endl << std::endl << output << std::endl;
  return {result.str()};
}

Result<Json::Value> CvdClient::ListSubcommands(const cvd_common::Envs& envs) {
  cvd_common::Args args{"cvd", "cmd-list"};
  SharedFD read_pipe, write_pipe;
  CF_EXPECT(cuttlefish::SharedFD::Pipe(&read_pipe, &write_pipe),
            "Unable to create shutdown pipe: " << strerror(errno));
  OverrideFd new_control_fd{.stdout_override_fd = write_pipe};
  CF_EXPECT(
      HandleCommand(args, envs, std::vector<std::string>{}, new_control_fd));

  write_pipe->Close();
  const int kChunkSize = 512;
  char buf[kChunkSize + 1] = {0};
  std::stringstream ss;
  do {
    auto n_read = ReadExact(read_pipe, buf, kChunkSize);
    CF_EXPECT(n_read >= 0 && (n_read <= kChunkSize));
    if (n_read == 0) {
      break;
    }
    buf[n_read] = 0;  // null-terminate the C-style string
    ss << buf;
    if (n_read < sizeof(buf) - 1) {
      break;
    }
  } while (true);
  auto json_output = CF_EXPECT(ParseJson(ss.str()));
  return json_output;
}

Result<cvd_common::Args> CvdClient::ValidSubcmdsList(
    const cvd_common::Envs& envs) {
  auto valid_subcmd_json = CF_EXPECT(ListSubcommands(envs));
  CF_EXPECT(valid_subcmd_json.isMember("subcmd"),
            "Server returned the list of subcommands in Json but it is missing "
                << " \"subcmd\" field");
  std::string valid_subcmd_string = valid_subcmd_json["subcmd"].asString();
  auto valid_subcmds = android::base::Tokenize(valid_subcmd_string, ",");
  return valid_subcmds;
}

}  // end of namespace cuttlefish
