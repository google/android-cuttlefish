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

#include <algorithm>
#include <iterator>
#include <limits>
#include <sstream>
#include <thread>
#include <vector>

#include <android-base/logging.h>
#include <gflags/gflags.h>

#include <signal.h>
#include <unistd.h>
#include <host/commands/kernel_log_monitor/kernel_log_server.h>

#include "common/libs/fs/tee.h"
#include "common/libs/fs/shared_fd.h"
#include "host/frontend/adb_connector/adb_connection_maintainer.h"
#include "host/libs/config/cuttlefish_config.h"

DEFINE_string(addresses, "", "Comma-separated list of addresses to "
                             "'adb connect' to");
DEFINE_int32(adbd_events_fd, -1, "A file descriptor. If set it will wait for "
                                 "AdbdStarted boot event from the kernel log "
                                 "monitor before trying to connect adb");

namespace {

std::vector<std::string> ParseAddressList(std::string ports) {
  std::replace(ports.begin(), ports.end(), ',', ' ');
  std::istringstream port_stream{ports};
  return {std::istream_iterator<std::string>{port_stream},
          std::istream_iterator<std::string>{}};
}

void WaitForAdbdToBeStarted(int events_fd) {
  auto evt_shared_fd = cvd::SharedFD::Dup(events_fd);
  close(events_fd);
  while (evt_shared_fd->IsOpen()) {
    monitor::BootEvent event;
    auto bytes_read = evt_shared_fd->Read(&event, sizeof(event));
    if (bytes_read != sizeof(event)) {
      LOG(ERROR) << "Fail to read a complete event, read " << bytes_read
                 << " bytes only instead of the expected " << sizeof(event);
      // The file descriptor can't be trusted anymore, stop waiting and try to
      // connect
      return;
    }
    if (event == monitor::BootEvent::AdbdStarted) {
      LOG(INFO) << "Adbd has started in the guest, connecting adb";
      return;
    }
  }
}
}  // namespace

int main(int argc, char* argv[]) {
  ::android::base::InitLogging(argv, android::base::StderrLogger);

  gflags::ParseCommandLineFlags(&argc, &argv, true);
  CHECK(!FLAGS_addresses.empty()) << "Must specify --addresses flag";

  if (FLAGS_adbd_events_fd >= 0) {
    WaitForAdbdToBeStarted(FLAGS_adbd_events_fd);
  }

  LOG(INFO) << "Blocking sighup, sigpipe";
  // These signal masks are inherited by threads.
  sigset_t sigmask;
  sigemptyset(&sigmask);
  sigaddset(&sigmask, SIGHUP);
  sigaddset(&sigmask, SIGPIPE);
  pthread_sigmask(SIG_BLOCK, &sigmask, nullptr);

  // Valuable for debugging issues with the exit handler.
  // Must be done after the signal mask is set up, so the "tee to stderr and a file"
  // thread inherits the signal mask and blocks the SIGPIPE that comes from its parent
  // stderr pipe read end closing.
  auto fd = cvd::SharedFD::Creat("adb_connector_logs.txt", 0755);
  cvd::TeeStderrToFile stderr_tee;
  stderr_tee.SetFile(fd);

  std::vector<std::thread> threads;

  std::atomic<bool> parent_alive = true;

  for (auto address : ParseAddressList(FLAGS_addresses)) {
    threads.emplace_back(cvd::EstablishAndMaintainConnection, address, &parent_alive);
  }

  while (true) {
    int signal_num;
    LOG(DEBUG) << "Waiting for next signal";
    int sigwait_ret = sigwait(&sigmask, &signal_num);
    if (sigwait_ret != 0) {
      LOG(ERROR) << "sigwait returned " << sigwait_ret;
      break;
    }
    LOG(INFO) << "Received signal " << strsignal(signal_num);
    if (signal_num == SIGHUP) {
      parent_alive = false;
      break;
    }
  }

  for (auto address : ParseAddressList(FLAGS_addresses)) {
    cvd::AdbDisconnect(address);
  }

  LOG(INFO) << "Waiting for threads";

  for (auto& thread : threads) {
    thread.join();
  }

  LOG(INFO) << "Exiting normally";
}
