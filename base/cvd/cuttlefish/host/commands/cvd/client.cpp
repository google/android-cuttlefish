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

#include "host/commands/cvd/client.h"

#include <unistd.h>

#include <android-base/file.h>
#include <google/protobuf/text_format.h>

#include "common/libs/fs/shared_fd.h"
#include "common/libs/utils/proto.h"
#include "common/libs/utils/result.h"
#include "host/commands/cvd/common_utils.h"

namespace cuttlefish {
namespace {

}  // end of namespace

Result<void> CvdClient::ConnectToServer() {
  if (server_) {
    return {};
  }
  auto connection =
      SharedFD::SocketLocalClient(server_socket_path_,
                                  /*is_abstract=*/true, SOCK_SEQPACKET);
  if (!connection->IsOpen()) {
    auto connection =
        SharedFD::SocketLocalClient(server_socket_path_,
                                    /*is_abstract=*/true, SOCK_STREAM);
  }
  if (!connection->IsOpen()) {
    return CF_ERR("Failed to connect to server" << connection->StrError());
  }

  CF_EXPECT(SetServer(connection));
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

Result<void> CvdClient::RestartServerMatchClient() {
  auto res = CF_EXPECT(HandleCommand({"cvd", "process"}, {},
                                     {"cvd", "restart-server", "match-client"},
                                     OverrideFd{
                                         SharedFD::Dup(0),
                                         SharedFD::Dup(1),
                                         SharedFD::Dup(2),
                                     }));
  if (res.status().code() != cvd::Status::OK) {
    return CF_ERRF("CVD server returned error: {}", res.error_response());
  }
  return {};
}

Result<cvd::Response> CvdClient::HandleCommand(
    const std::vector<std::string>& cvd_process_args,
    const std::unordered_map<std::string, std::string>& env,
    const std::vector<std::string>& selector_args,
    const OverrideFd& new_control_fd) {
  std::optional<SharedFD> exe_fd;
  // actual commandline arguments are packed in selector_args
  if (selector_args.size() > 2 &&
      android::base::Basename(selector_args[0]) == "cvd" &&
      selector_args[1] == "restart-server" &&
      selector_args[2] == "match-client") {
    exe_fd = SharedFD::Open(kServerExecPath, O_RDONLY);
    CF_EXPECT((*exe_fd)->IsOpen(), "Failed to open \""
                                       << kServerExecPath << "\": \""
                                       << (*exe_fd)->StrError() << "\"");
  }
  cvd::Request request = MakeRequest({.cmd_args = cvd_process_args,
                                      .env = env,
                                      .selector_args = selector_args},
                                     cvd::WAIT_BEHAVIOR_COMPLETE);
  return CF_EXPECT(SendRequest(request, new_control_fd, exe_fd));
}

Result<void> CvdClient::SetServer(const SharedFD& server) {
  CF_EXPECT(!server_, "Already have a server");
  CF_EXPECT(server->IsOpen(), server->StrError());
  server_ = UnixMessageSocket(server);
  CF_EXPECT(server_->EnableCredentials(true).ok(),
            "Unable to enable UnixMessageSocket credentials.");
  return {};
}

Result<cvd::Response> CvdClient::SendRequest(const cvd::Request& request_orig,
                                             const OverrideFd& new_control_fds,
                                             std::optional<SharedFD> extra_fd) {
  CF_EXPECT(ConnectToServer());
  cvd::Request request(request_orig);
  auto* verbosity = request.mutable_verbosity();
  *verbosity = CF_EXPECT(VerbosityToString(verbosity_));

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

Result<void> CvdClient::CheckStatus(const cvd::Status& status,
                                    const std::string& rpc) {
  if (status.code() == cvd::Status::OK) {
    return {};
  }
  return CF_ERRF("Received error response for \"{}\"\n{}\n\n{}\n{}", rpc,
                 "*** End of Client Stack Trace ***", status.message(),
                 "*** End of Server Stack Trace/Error ***");
}

CvdClient::CvdClient(const android::base::LogSeverity verbosity,
                     const std::string& server_socket_path)
    : server_socket_path_(server_socket_path), verbosity_(verbosity) {}

}  // end of namespace cuttlefish
