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

#include <signal.h>

#include <android-base/logging.h>
#include <gflags/gflags.h>

#include "common/libs/fs/shared_fd.h"
#include "common/libs/utils/socket2socket_proxy.h"
#include "host/libs/config/logging.h"

DEFINE_int32(server_port, 8443, "The port for the proxy server");
DEFINE_int32(operator_port, 1443, "The port of the operator server to proxy");

cuttlefish::SharedFD OpenConnection() {
  auto conn =
      cuttlefish::SharedFD::SocketLocalClient(FLAGS_operator_port, SOCK_STREAM);
  if (!conn->IsOpen()) {
    LOG(ERROR) << "Failed to connect to operator: " << conn->StrError();
  }
  return conn;
}

int main(int argc, char** argv) {
  cuttlefish::DefaultSubprocessLogging(argv);
  ::gflags::ParseCommandLineFlags(&argc, &argv, true);

  auto server =
      cuttlefish::SharedFD::SocketLocalServer(FLAGS_server_port, SOCK_STREAM);
  CHECK(server->IsOpen()) << "Error Creating proxy server: "
                          << server->StrError();

  signal(SIGPIPE, SIG_IGN);

  cuttlefish::Proxy("operator_proxy", server, OpenConnection);

  return 0;
}
