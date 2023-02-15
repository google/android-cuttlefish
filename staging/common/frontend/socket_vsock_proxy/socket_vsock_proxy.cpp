/*
 * Copyright (C) 2018 The Android Open Source Project
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

#include <memory>
#include <sstream>

#include "common/frontend/socket_vsock_proxy/client.h"
#include "common/frontend/socket_vsock_proxy/server.h"
#include "common/libs/fs/shared_fd.h"
#include "common/libs/utils/socket2socket_proxy.h"
#include "host/commands/kernel_log_monitor/utils.h"

#ifdef CUTTLEFISH_HOST
#include "host/libs/config/logging.h"
#endif // CUTTLEFISH_HOST

constexpr const char TRANSPORT_TCP[] = "tcp";
constexpr const char TRANSPORT_VSOCK[] = "vsock";

DEFINE_string(server_type, "",
              "The type of server to host, `vsock` or `tcp`.");
DEFINE_string(client_type, "",
              "The type of server to host, `vsock` or `tcp`.");
DEFINE_uint32(server_tcp_port, 0, "Server TCP port");
DEFINE_string(client_tcp_host, "localhost", "Client TCP host (default localhost)");
DEFINE_uint32(client_tcp_port, 0, "Client TCP port");
DEFINE_uint32(server_vsock_port, 0, "vsock port");
DEFINE_uint32(client_vsock_id, 0, "Vsock cid to initiate connections to");
DEFINE_uint32(client_vsock_port, 0, "Vsock port to initiate connections to");
DEFINE_int32(adbd_events_fd, -1, "A file descriptor. If set it will wait for "
                                 "AdbdStarted boot event from the kernel log "
                                 "monitor before creating a tcp-vsock tunnel."
                                 "This option is used by --server=tcp only "
                                 "when socket_vsock_proxy runs as a host service");
DEFINE_int32(
    server_fd, -1,
    "A file descriptor. If set the passed file descriptor will be used as the "
    "server and the corresponding port flag will be ignored");

DEFINE_string(label, "socket_vsock_proxy",
              "Label which is used only for logging. Log messages will look like [label] message");

namespace cuttlefish {
namespace socket_proxy {
namespace {

void WaitForAdbdToBeStarted(int events_fd) {
  auto evt_shared_fd = SharedFD::Dup(events_fd);
  close(events_fd);
  while (evt_shared_fd->IsOpen()) {
    std::optional<monitor::ReadEventResult> read_result =
        monitor::ReadEvent(evt_shared_fd);
    if (!read_result) {
      LOG(ERROR) << "[" << FLAGS_label << "] Failed to read a complete kernel log adb event.";
      // The file descriptor can't be trusted anymore, stop waiting and try to
      // connect
      return;
    }

    if (read_result->event == monitor::Event::AdbdStarted) {
      LOG(DEBUG) << "[" << FLAGS_label << "] Adbd has started in the guest, connecting adb";
      return;
    }
  }
}

std::unique_ptr<Server> BuildServer() {
  if (FLAGS_server_fd >= 0) {
    LOG(INFO) << "[" << FLAGS_label << "] From: fd: " << FLAGS_server_fd;
    return std::make_unique<DupServer>(FLAGS_server_fd);
  }

  CHECK(FLAGS_server_type == TRANSPORT_TCP || FLAGS_server_type == TRANSPORT_VSOCK)
      << "Must specify -server_type with tcp or vsock values";

  if (FLAGS_server_type == TRANSPORT_TCP) {
    CHECK(FLAGS_server_tcp_port != 0)
        << "Must specify -server_tcp_port or -server_fd with -server_type=tcp flag";
  }
  if (FLAGS_server_type == TRANSPORT_VSOCK) {
    CHECK(FLAGS_server_vsock_port != 0)
        << "Must specify -server_vsock_port or -server_fd with -server_type=vsock flag";
  }

  std::unique_ptr<Server> server = nullptr;

  if (FLAGS_server_type == TRANSPORT_TCP) {
    LOG(INFO) << "[" << FLAGS_label << "] From: tcp: " << FLAGS_server_tcp_port;
    server = std::make_unique<TcpServer>(FLAGS_server_tcp_port);
  } else if (FLAGS_server_type == TRANSPORT_VSOCK) {
    LOG(INFO) << "[" << FLAGS_label << "] From: vsock: " << FLAGS_server_vsock_port;
    server = std::make_unique<VsockServer>(FLAGS_server_vsock_port);
  } else {
    LOG(FATAL) << "[" << FLAGS_label << "] Unknown server type: " << FLAGS_server_type;
  }

  return server;
}

std::unique_ptr<Client> BuildClient() {
  CHECK(FLAGS_client_type == TRANSPORT_TCP || FLAGS_client_type == TRANSPORT_VSOCK)
      << "Must specify -client_type with tcp or vsock values";

  if (FLAGS_client_type == TRANSPORT_TCP) {
    CHECK(FLAGS_client_tcp_port != 0)
        << "For -client_type=tcp you must specify -client_tcp_port flag";
  }
  if (FLAGS_client_type == TRANSPORT_VSOCK) {
    CHECK(FLAGS_client_vsock_id >= 0 && FLAGS_client_vsock_port >= 0)
        << "For -client_type=vsock you must specify -client_vsock_id and -client_vsock_port flags";
  }

  std::unique_ptr<Client> client = nullptr;

  if (FLAGS_client_type == TRANSPORT_TCP) {
    LOG(INFO) << "[" << FLAGS_label << "] To: tcp: "
              << FLAGS_client_tcp_host << ":" << FLAGS_client_tcp_port;
    client = std::make_unique<TcpClient>(FLAGS_client_tcp_host, FLAGS_client_tcp_port);
  } else if (FLAGS_client_type == TRANSPORT_VSOCK) {
    LOG(INFO) << "[" << FLAGS_label << "] To: vsock: "
              << FLAGS_client_vsock_id << ":" << FLAGS_client_vsock_port;
    client = std::make_unique<VsockClient>(FLAGS_client_vsock_id, FLAGS_client_vsock_port);
  } else {
    LOG(FATAL) << "[" << FLAGS_label << "] Unknown client type: " << FLAGS_client_type;
  }

  return client;
}

}
}
}

int main(int argc, char* argv[]) {
  signal(SIGPIPE, SIG_IGN);

#ifdef CUTTLEFISH_HOST
  cuttlefish::DefaultSubprocessLogging(argv);
#else
  ::android::base::InitLogging(argv, android::base::LogdLogger(android::base::SYSTEM));
#endif
  google::ParseCommandLineFlags(&argc, &argv, true);

  if (FLAGS_adbd_events_fd >= 0) {
    LOG(DEBUG) << "[" << FLAGS_label << "] Wating AdbdStarted boot event from the kernel log";
    cuttlefish::socket_proxy::WaitForAdbdToBeStarted(FLAGS_adbd_events_fd);
  }

  auto server = cuttlefish::socket_proxy::BuildServer();
  auto client = cuttlefish::socket_proxy::BuildClient();

  LOG(DEBUG) << "[" << FLAGS_label << "] Accepting client connections";
  auto proxy = cuttlefish::ProxyAsync(FLAGS_label, server->Start(), [&client] {
    return client->Start();
  });
  proxy->Join();
}
