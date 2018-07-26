/*
 * Copyright (C) 2017 The Android Open Source Project
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

#include <gflags/gflags.h>
#include <glog/logging.h>

#include <common/libs/fs/shared_fd.h>
#include <common/libs/fs/shared_select.h>
#include <host/libs/config/cuttlefish_config.h>
#include "host/libs/monitor/kernel_log_server.h"

DEFINE_int32(log_server_fd, -1,
             "A file descriptor representing a (UNIX) socket from which to "
             "read the logs. If -1 is given the socket is created according to "
             "the instance configuration");
DEFINE_int32(subscriber_fd, -1,
             "A file descriptor (a pipe) to write boot events to. If -1 is "
             "given no events will be sent");

int main(int argc, char** argv) {
  ::android::base::InitLogging(argv, android::base::StderrLogger);
  google::ParseCommandLineFlags(&argc, &argv, true);

  // Disable default handling of SIGPIPE
  struct sigaction new_action {
  }, old_action{};
  new_action.sa_handler = SIG_IGN;
  sigaction(SIGPIPE, &new_action, &old_action);

  auto config = vsoc::CuttlefishConfig::Get();
  if (!config) {
    LOG(ERROR) << "Unable to get config object";
    return 1;
  }

  cvd::SharedFD server;
  if (FLAGS_log_server_fd < 0) {
    auto log_name = config->kernel_log_socket_name();
    server = cvd::SharedFD::SocketLocalServer(log_name.c_str(), false,
                                              SOCK_STREAM, 0666);
  } else {
    server = cvd::SharedFD::Dup(FLAGS_log_server_fd);
    close(FLAGS_log_server_fd);
  }

  if (!server->IsOpen()) {
    LOG(ERROR) << "Error opening log server: " << server->StrError();
    return 2;
  }

  monitor::KernelLogServer klog{server, config->PerInstancePath("kernel.log"),
                                config->deprecated_boot_completed()};

  if (FLAGS_subscriber_fd >= 0) {
    auto pipe_fd = cvd::SharedFD::Dup(FLAGS_subscriber_fd);
    close(FLAGS_subscriber_fd);
    if (pipe_fd->IsOpen()) {
      klog.SubscribeToBootEvents([pipe_fd](monitor::BootEvent evt) {
        int retval = pipe_fd->Write(&evt, sizeof(evt));
        if (retval < 0) {
          if (pipe_fd->GetErrno() != EPIPE) {
            LOG(ERROR) << "Error while writing to pipe: "
                       << pipe_fd->StrError();
          }
          pipe_fd->Close();
          return monitor::SubscriptionAction::CancelSubscription;
        }
        return monitor::SubscriptionAction::ContinueSubscription;
      });
    } else {
      LOG(ERROR) << "Subscriber fd isn't valid: " << pipe_fd->StrError();
      // Don't return here, we still need to write the logs to a file
    }
  }

  for (;;) {
    cvd::SharedFDSet fd_read;
    fd_read.Zero();

    klog.BeforeSelect(&fd_read);

    int ret = cvd::Select(&fd_read, nullptr, nullptr, nullptr);
    if (ret <= 0) continue;

    klog.AfterSelect(fd_read);
  }

  return 0;
}
