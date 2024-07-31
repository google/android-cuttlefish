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

#include <chrono>
#include <memory>
#include <sstream>

#include "common/frontend/socket_vsock_proxy/client.h"
#include "common/frontend/socket_vsock_proxy/server.h"
#include "common/libs/fs/shared_fd.h"
#include "common/libs/utils/result.h"
#include "common/libs/utils/socket2socket_proxy.h"
#include "common/libs/utils/tee_logging.h"
#include "host/commands/kernel_log_monitor/utils.h"

#ifdef CUTTLEFISH_HOST
#include "host/libs/config/cuttlefish_config.h"
#include "host/libs/config/logging.h"
#endif // CUTTLEFISH_HOST

constexpr int TCP_SERVER_START_RETRIES_COUNT = 10;
constexpr std::chrono::milliseconds TCP_SERVER_RETRIES_DELAY(1250);
constexpr std::chrono::seconds TCP_CLIENT_TIMEOUT(1);

constexpr char TRANSPORT_TCP[] = "tcp";
constexpr char TRANSPORT_VSOCK[] = "vsock";

DEFINE_string(label, "socket_vsock_proxy", "Label which is used only for logging. "
                                           "Log messages will look like [label] message");
DEFINE_string(server_type, "", "The type of server to host, `vsock` or `tcp`.");
DEFINE_string(client_type, "", "The type of server to host, `vsock` or `tcp`.");
DEFINE_uint32(server_tcp_port, 0, "Server TCP port");
DEFINE_string(client_tcp_host, "localhost", "Client TCP host (default localhost)");
DEFINE_uint32(client_tcp_port, 0, "Client TCP port");
DEFINE_uint32(server_vsock_port, 0, "vsock port");
DEFINE_uint32(server_vsock_id, 0, "Vsock cid which server listens to");
DEFINE_uint32(client_vsock_id, 0, "Vsock cid to initiate connections to");
DEFINE_uint32(client_vsock_port, 0, "Vsock port to initiate connections to");
DEFINE_int32(server_fd, -1, "A file descriptor. If set the passed file descriptor will be used as "
                            "the server and the corresponding port flag will be ignored");

DEFINE_uint32(events_fd, -1, "A file descriptor. If set it will listen for the events "
                             "to start / stop proxying. This option can be used only "
                             "if start_event_id is provided (stop_event_id is optional)");
DEFINE_uint32(start_event_id, -1, "Kernel event id (cuttlefish::monitor::Event from "
                                  "kernel_log_server.h) that we will listen to start proxy");
DEFINE_uint32(stop_event_id, -1, "Kernel event id (cuttlefish::monitor::Event from "
                                  "kernel_log_server.h) that we will listen to stop proxy");
#ifdef CUTTLEFISH_HOST
DEFINE_bool(restore, false,
            "Wait on the restore_adbd_pipe instead of the initial start event");
#endif
DEFINE_bool(vhost_user_vsock, false, "A flag to user vhost_user_vsock");

namespace cuttlefish {
namespace socket_proxy {
namespace {
static bool use_vhost_vsock() {
#ifdef CUTTLEFISH_HOST
  return FLAGS_vhost_user_vsock;
#else
  return false;
#endif
}
static std::unique_ptr<Server> BuildServer() {
  if (FLAGS_server_fd >= 0) {
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
    if (use_vhost_vsock()) {
      CHECK(FLAGS_server_vsock_id > VMADDR_CID_HOST)
          << "Must specify --server_vsock_id with --vhost_user_vsock=true flag";
    }
  }

  std::unique_ptr<Server> server = nullptr;

  if (FLAGS_server_type == TRANSPORT_TCP) {
    server = std::make_unique<TcpServer>(FLAGS_server_tcp_port, TCP_SERVER_START_RETRIES_COUNT,
                                         TCP_SERVER_RETRIES_DELAY);
  } else if (FLAGS_server_type == TRANSPORT_VSOCK) {
    server = std::make_unique<VsockServer>(
        FLAGS_server_vsock_port, use_vhost_vsock()
                                     ? std::make_optional(FLAGS_server_vsock_id)
                                     : std::nullopt);
  } else {
    LOG(FATAL) << "Unknown server type: " << FLAGS_server_type;
  }

  return server;
}

static std::unique_ptr<Client> BuildClient() {
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
    client = std::make_unique<TcpClient>(FLAGS_client_tcp_host, FLAGS_client_tcp_port,
                                         TCP_CLIENT_TIMEOUT);
  } else if (FLAGS_client_type == TRANSPORT_VSOCK) {
    client = std::make_unique<VsockClient>(
        FLAGS_client_vsock_id, FLAGS_client_vsock_port, use_vhost_vsock());
  } else {
    LOG(FATAL) << "Unknown client type: " << FLAGS_client_type;
  }

  return client;
}

static Result<std::unique_ptr<ProxyServer>> StartProxyAsync(Server& server, Client& client) {
  LOG(INFO) << "From: " << server.Describe();
  LOG(INFO) << "To: " << client.Describe();
  return ProxyAsync(CF_EXPECT(server.Start()), [&client] { return client.Start(); });
}

static Result<void> ListenEventsAndProxy(int events_fd,
                                         const monitor::Event start, const monitor::Event stop,
                                         Server& server, Client& client) {
  auto events = SharedFD::Dup(events_fd);
  close(events_fd);

  std::unique_ptr<ProxyServer> proxy;

#ifdef CUTTLEFISH_HOST
  if (FLAGS_restore) {
    LOG(INFO) << "restoring proxy on CUTTLEFISH_HOST - wait for adbd to come "
                 "online before starting proxy";
    auto config = CF_EXPECT(CuttlefishConfig::Get());
    auto instance = config->ForDefaultInstance();
    SharedFD restore_pipe_read =
        SharedFD::Open(instance.restore_adbd_pipe_name(), O_RDONLY);
    if (!restore_pipe_read->IsOpen()) {
      return CF_ERR(
          "Error opening restore pipe: " << restore_pipe_read->StrError());
    }
    // Try to read from restore pipe. IF successfully reads, that means logcat
    // has started, and the VM has resumed. Exit the thread.
    // TODO (@khei): Add a device status tracking mechanism. b/325614380
    char buff[1];
    auto read = restore_pipe_read->Read(buff, 1);
    if (read <= 0) {
      return CF_ERR(
          "Could not read restore pipe: " << restore_pipe_read->StrError());
    }
    LOG(INFO) << "restoring proxy on CUTTLEFISH_HOST - success";
    proxy = std::move(CF_EXPECT(StartProxyAsync(server, client)));
  }
#endif

  LOG(DEBUG) << "Start reading events to start/stop proxying";
  while (events->IsOpen()) {
    Result<std::optional<monitor::ReadEventResult>> received_event =
        monitor::ReadEvent(events);

    // TODO(schuffelen): Investigate if any errors here are recoverable, and
    // remove the distinction between EOF and other errors if none are
    // recoverable.
    if (!received_event) {
      LOG(ERROR) << "Failed reading kernel log event: "
                 << received_event.error().FormatForEnv();
      continue;
    }
    if (!(*received_event)) {
      LOG(DEBUG) << "Kernel log message channel closed";
      break;
    }

    if (start != -1 && (*received_event)->event == start) {
      if (!proxy) {
        LOG(INFO) << "Start event (" << start << ") received. Starting proxy";
        proxy = std::move(CF_EXPECT(StartProxyAsync(server, client)));
      }
      continue;
    }

    if (stop != -1 && (*received_event)->event == stop) {
      LOG(INFO) << "Stop event (" << start << ") received. Stopping proxy";
      proxy.reset();
      continue;
    }
  }

  return {};
}

Result<void> Main() {
  auto server = BuildServer();
  auto client = BuildClient();

  if (FLAGS_events_fd != -1) {
    CF_EXPECT(FLAGS_start_event_id != -1, "start_event_id is required if events_fd is provided");

    const monitor::Event start_event = static_cast<monitor::Event>(FLAGS_start_event_id);
    const monitor::Event stop_event = static_cast<monitor::Event>(FLAGS_stop_event_id);

    CF_EXPECT(ListenEventsAndProxy(FLAGS_events_fd, start_event, stop_event,
                                   *server, *client));
  } else {
    LOG(DEBUG) << "Starting proxy";
    Proxy(CF_EXPECT(server->Start()), [&client] { return client->Start(); });
  }

  return {};
}

}
}
}

int main(int argc, char* argv[]) {
  signal(SIGPIPE, SIG_IGN);

#ifdef CUTTLEFISH_HOST
  cuttlefish::DefaultSubprocessLogging(argv, cuttlefish::MetadataLevel::TAG_AND_MESSAGE);
#else
  ::android::base::InitLogging(argv, android::base::LogdLogger(android::base::SYSTEM));
#endif
  google::ParseCommandLineFlags(&argc, &argv, true);

  if (!FLAGS_label.empty()) {
    android::base::SetDefaultTag("proxy_" + FLAGS_label);
  }

  auto result = cuttlefish::socket_proxy::Main();
  if (!result.ok()) {
    LOG(FATAL) << "Failed to proxy: " << result.error().FormatForEnv();
  }

  return 0;
}
