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
#include <unistd.h>

#include <string>
#include <vector>

#include <android-base/strings.h>
#include <gflags/gflags.h>
#include <android-base/logging.h>

#include <common/libs/fs/shared_fd.h>
#include <common/libs/fs/shared_select.h>
#include <host/libs/config/cuttlefish_config.h>
#include "host/commands/kernel_log_monitor/kernel_log_server.h"

DEFINE_int32(log_pipe_fd, -1,
             "A file descriptor representing a (UNIX) socket from which to "
             "read the logs. If -1 is given the socket is created according to "
             "the instance configuration");
DEFINE_string(subscriber_fds, "",
             "A comma separated list of file descriptors (most likely pipes) to"
             " send boot events to.");

std::vector<cvd::SharedFD> SubscribersFromCmdline() {
  // Validate the parameter
  std::string fd_list = FLAGS_subscriber_fds;
  for (auto c: fd_list) {
    if (c != ',' && (c < '0' || c > '9')) {
      LOG(ERROR) << "Invalid file descriptor list: " << fd_list;
      std::exit(1);
    }
  }

  auto fds = android::base::Split(FLAGS_subscriber_fds, ",");
  std::vector<cvd::SharedFD> shared_fds;
  for (auto& fd_str: fds) {
    auto fd = std::stoi(fd_str);
    auto shared_fd = cvd::SharedFD::Dup(fd);
    close(fd);
    shared_fds.push_back(shared_fd);
  }

  return shared_fds;
}

int main(int argc, char** argv) {
  ::android::base::InitLogging(argv, android::base::StderrLogger);
  google::ParseCommandLineFlags(&argc, &argv, true);

  auto subscriber_fds = SubscribersFromCmdline();

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
  auto instance = config->ForDefaultInstance();

  cvd::SharedFD pipe;
  if (FLAGS_log_pipe_fd < 0) {
    auto log_name = instance.kernel_log_pipe_name();
    pipe = cvd::SharedFD::Open(log_name.c_str(), O_RDONLY);
  } else {
    pipe = cvd::SharedFD::Dup(FLAGS_log_pipe_fd);
    close(FLAGS_log_pipe_fd);
  }

  if (!pipe->IsOpen()) {
    LOG(ERROR) << "Error opening log pipe: " << pipe->StrError();
    return 2;
  }

  monitor::KernelLogServer klog{pipe, instance.PerInstancePath("kernel.log"),
                                config->deprecated_boot_completed()};

  for (auto subscriber_fd: subscriber_fds) {
    if (subscriber_fd->IsOpen()) {
      klog.SubscribeToBootEvents([subscriber_fd](monitor::BootEvent evt) {
        int retval = subscriber_fd->Write(&evt, sizeof(evt));
        if (retval < 0) {
          if (subscriber_fd->GetErrno() != EPIPE) {
            LOG(ERROR) << "Error while writing to pipe: "
                       << subscriber_fd->StrError();
          }
          subscriber_fd->Close();
          return monitor::SubscriptionAction::CancelSubscription;
        }
        return monitor::SubscriptionAction::ContinueSubscription;
      });
    } else {
      LOG(ERROR) << "Subscriber fd isn't valid: " << subscriber_fd->StrError();
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
