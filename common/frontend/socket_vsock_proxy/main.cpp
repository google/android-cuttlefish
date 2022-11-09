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

#include <set>
#include <android-base/logging.h>
#include <gflags/gflags.h>

#include "common/libs/fs/shared_fd.h"
#include "common/libs/utils/contains.h"
#include "common/libs/utils/socket2socket_proxy.h"
#include "host/commands/kernel_log_monitor/utils.h"

#ifdef CUTTLEFISH_HOST
#include "host/libs/config/logging.h"
#endif // CUTTLEFISH_HOST

DEFINE_string(server, "",
              "The type of server to host, `vsock` or `tcp`. When hosting a server "
              "of one type, the proxy will take inbound connections of this type and "
              "make outbound connections of the other type.");
DEFINE_uint32(tcp_port, 0, "TCP port");
DEFINE_uint32(vsock_port, 0, "vsock port");
DEFINE_uint32(vsock_cid, 0, "Vsock cid to initiate connections to");
DEFINE_int32(adbd_events_fd, -1, "A file descriptor. If set it will wait for "
                                 "AdbdStarted boot event from the kernel log "
                                 "monitor before creating a tcp-vsock tunnel."
                                 "This option is used by --server=tcp only "
                                 "when socket_vsock_proxy runs as a host service");
DEFINE_int32(
    server_fd, -1,
    "A file descriptor. If set the passed file descriptor will be used as the "
    "server and the corresponding port flag will be ignored");

namespace {
void WaitForAdbdToBeStarted(int events_fd) {
  auto evt_shared_fd = cuttlefish::SharedFD::Dup(events_fd);
  close(events_fd);
  while (evt_shared_fd->IsOpen()) {
    std::optional<monitor::ReadEventResult> read_result =
        monitor::ReadEvent(evt_shared_fd);
    if (!read_result) {
      LOG(ERROR) << "Failed to read a complete kernel log adb event.";
      // The file descriptor can't be trusted anymore, stop waiting and try to
      // connect
      return;
    }

    if (read_result->event == monitor::Event::AdbdStarted) {
      LOG(DEBUG) << "Adbd has started in the guest, connecting adb";
      return;
    }
  }
}

// intented to run as cuttlefish host service
void TcpServer() {
  LOG(DEBUG) << "starting TCP server on " << FLAGS_tcp_port
             << " for vsock port " << FLAGS_vsock_port;
  cuttlefish::SharedFD server;
  if (FLAGS_server_fd < 0) {
    server =
        cuttlefish::SharedFD::SocketLocalServer(FLAGS_tcp_port, SOCK_STREAM);
  } else {
    server = cuttlefish::SharedFD::Dup(FLAGS_server_fd);
    close(FLAGS_server_fd);
  }
  CHECK(server->IsOpen()) << "Could not start server on " << FLAGS_tcp_port;
  LOG(DEBUG) << "Accepting client connections";
  int last_failure_reason = 0;
  cuttlefish::Proxy(server, [&last_failure_reason]() {
    auto vsock_socket = cuttlefish::SharedFD::VsockClient(
        FLAGS_vsock_cid, FLAGS_vsock_port, SOCK_STREAM);
    if (vsock_socket->IsOpen()) {
      last_failure_reason = 0;
      LOG(DEBUG) << "Connected to vsock:" << FLAGS_vsock_cid << ":"
                 << FLAGS_vsock_port;
    } else {
      // Don't log if the previous connection failed with the same error
      if (last_failure_reason != vsock_socket->GetErrno()) {
        last_failure_reason = vsock_socket->GetErrno();
        LOG(ERROR) << "Unable to connect to vsock server: "
                   << vsock_socket->StrError();
      }
    }
    return vsock_socket;
  });
}

cuttlefish::SharedFD OpenSocketConnection() {
  while (true) {
    auto sock = cuttlefish::SharedFD::SocketLocalClient(FLAGS_tcp_port, SOCK_STREAM);
    if (sock->IsOpen()) {
      return sock;
    }
    LOG(WARNING) << "could not connect on port " << FLAGS_tcp_port
                 << ". sleeping for 1 second";
    sleep(1);
  }
}

bool socketErrorIsRecoverable(int error) {
  std::set<int> unrecoverable{EACCES, EAFNOSUPPORT, EINVAL, EPROTONOSUPPORT};
  return !cuttlefish::Contains(unrecoverable, error);
}

[[noreturn]] static void SleepForever() {
  while (true) {
    sleep(std::numeric_limits<unsigned int>::max());
  }
}

// intended to run inside Android guest
void VsockServer() {
  LOG(DEBUG) << "Starting vsock server on " << FLAGS_vsock_port;
  cuttlefish::SharedFD vsock;
  if (FLAGS_server_fd < 0) {
    do {
      vsock = cuttlefish::SharedFD::VsockServer(FLAGS_vsock_port, SOCK_STREAM);
      if (!vsock->IsOpen() && !socketErrorIsRecoverable(vsock->GetErrno())) {
        LOG(ERROR) << "Could not open vsock socket: " << vsock->StrError();
        SleepForever();
      }
    } while (!vsock->IsOpen());
  } else {
    vsock = cuttlefish::SharedFD::Dup(FLAGS_server_fd);
    close(FLAGS_server_fd);
  }
  CHECK(vsock->IsOpen()) << "Could not start server on " << FLAGS_vsock_port;
  cuttlefish::Proxy(vsock, []() {
    LOG(DEBUG) << "vsock socket accepted";
    auto client = OpenSocketConnection();
    CHECK(client->IsOpen()) << "error connecting to guest client";
    return client;
  });
}

}  // namespace

int main(int argc, char* argv[]) {
#ifdef CUTTLEFISH_HOST
  cuttlefish::DefaultSubprocessLogging(argv);
#else
  ::android::base::InitLogging(argv, android::base::LogdLogger());
#endif
  google::ParseCommandLineFlags(&argc, &argv, true);

  CHECK((FLAGS_server == "tcp" && FLAGS_server_fd >= 0) || FLAGS_tcp_port != 0)
      << "Must specify -tcp_port or -server_fd (with -server=tcp) flag";
  CHECK((FLAGS_server == "vsock" && FLAGS_server_fd >= 0) ||
        FLAGS_vsock_port != 0)
      << "Must specify -vsock_port or -server_fd (with -server=vsock) flag";

  if (FLAGS_adbd_events_fd >= 0) {
    LOG(DEBUG) << "Wating AdbdStarted boot event from the kernel log";
    WaitForAdbdToBeStarted(FLAGS_adbd_events_fd);
  }

  if (FLAGS_server == "tcp") {
    CHECK(FLAGS_vsock_cid != 0) << "Must specify -vsock_cid flag";
    TcpServer();
  } else if (FLAGS_server == "vsock") {
    VsockServer();
  } else {
    LOG(FATAL) << "Unknown server type: " << FLAGS_server;
  }
}
