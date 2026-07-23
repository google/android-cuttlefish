/*
 * Copyright (C) 2026 The Android Open Source Project
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

#include "cuttlefish/host/commands/cvd/cli/commands/monitor/monitor.h"

#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/eventfd.h>
#include <sys/inotify.h>
#include <sys/poll.h>
#include <unistd.h>

#include <chrono>
#include <iostream>
#include <memory>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "absl/cleanup/cleanup.h"
#include "absl/strings/str_cat.h"

#include "cuttlefish/ansi_codes/ansi_codes.h"
#include "cuttlefish/common/libs/fs/shared_fd.h"
#include "cuttlefish/host/commands/cvd/cli/commands/monitor/display.h"
#include "cuttlefish/host/commands/cvd/cli/commands/monitor/drain_inotify.h"
#include "cuttlefish/host/commands/cvd/cli/commands/monitor/log_sources.h"
#include "cuttlefish/host/commands/cvd/cli/commands/monitor/monitor_source.h"
#include "cuttlefish/host/commands/cvd/cli/utils.h"
#include "cuttlefish/host/commands/cvd/instances/local_instance.h"
#include "cuttlefish/result/result.h"

namespace cuttlefish {

Result<void> MonitorLogs(const LocalInstance& instance, SharedFD stop_eventfd) {
  CF_EXPECT(isatty(0), "The monitor command requires an interactive terminal.");

  std::unique_ptr<MonitorSource> kernel_monitor_source;
  std::unique_ptr<MonitorSource> launcher_monitor_source;
  std::unique_ptr<MonitorSource> logcat_monitor_source;

  const SharedFD dir_inotify_fd = SharedFD::InotifyFd();
  CF_EXPECT(dir_inotify_fd->IsOpen(), "Failed to create inotify fd");
  const int flags = dir_inotify_fd->Fcntl(F_GETFL, 0);
  CF_EXPECT(dir_inotify_fd->Fcntl(F_SETFL, flags | O_NONBLOCK) != -1,
            "Failed to set inotify fd to non-blocking");

  const std::string logs_dir =
      absl::StrCat(instance.InstanceDirectory(), "/logs");
  dir_inotify_fd->InotifyAddWatch(logs_dir, IN_CREATE | IN_DELETE);

  std::chrono::steady_clock::time_point last_draw_time =
      std::chrono::steady_clock::time_point();

  std::cout << kXtermUseAlternateScreen;
  std::cout.flush();
  absl::Cleanup clean_terminal = [] {
    std::cout << kAnsiReset << kAnsiClearScreen << kXtermUseMainScreen;
    std::cout.flush();
  };

  while (true) {
    if (!kernel_monitor_source.get()) {
      kernel_monitor_source = KernelLogMonitorSource(instance);
    }
    if (!logcat_monitor_source.get()) {
      logcat_monitor_source = LogcatMonitorSource(instance);
    }
    if (!launcher_monitor_source.get()) {
      launcher_monitor_source = LauncherLogMonitorSource(instance);
    }

    TerminalSize term_size =
        GetTerminalSize().value_or(TerminalSize{.rows = 35, .columns = 80});
    term_size.rows -= 3;
    term_size.columns -= 1;
    LogMonitorDisplay display(term_size.columns);

    bool missing_source = false;
    const std::vector<MonitorSource*> sources = {
        launcher_monitor_source.get(),
        kernel_monitor_source.get(),
        logcat_monitor_source.get(),
    };

    for (size_t i = 0; i < sources.size(); i++) {
      MonitorSource* source = sources[i];
      if (source == nullptr) {
        missing_source = true;
      }
      size_t lines = (term_size.rows / 3) - 1;
      while (i + 1 < sources.size() && sources[i + 1] == nullptr) {
        lines += (term_size.rows / 3) - 1;
        i++;
        missing_source = true;
      }
      display.DrawReport(source, lines);
    }

    const auto [output, total_lines_drawn] = display.Finalize();
    std::cout << kAnsiClearScreen << kAnsiCursorTopLeft << output << std::flush;

    // Enforce a maximum framerate (max 20 FPS / min 50ms between draws)
    // so we don't saturate SSH bandwidth or CPU during heavy, continuous
    // logging.
    const std::chrono::steady_clock::time_point now =
        std::chrono::steady_clock::now();
    const std::chrono::steady_clock::duration elapsed = now - last_draw_time;
    constexpr std::chrono::milliseconds min_frame_time(50);
    if (elapsed < min_frame_time) {
      std::this_thread::sleep_for(min_frame_time - elapsed);
    }
    last_draw_time = std::chrono::steady_clock::now();

    // Setup poll_fds
    std::vector<PollSharedFd> poll_fds = {
        PollSharedFd{.fd = dir_inotify_fd, .events = POLLIN, .revents = 0},
    };
    if (stop_eventfd->IsOpen()) {
      poll_fds.push_back(
          PollSharedFd{.fd = stop_eventfd, .events = POLLIN, .revents = 0});
    }
    for (MonitorSource* source : sources) {
      if (source == nullptr) {
        continue;
      }
      SharedFD ready_fd = source->ReadyFd();
      if (!ready_fd->IsOpen()) {
        missing_source = true;
        continue;
      }
      poll_fds.push_back(
          PollSharedFd{.fd = ready_fd, .events = POLLIN, .revents = 0});
    }

    // Block until file changes occur. If any watch failed or is missing, use
    // a fallback timeout to awake and retry.
    int poll_res = SharedFD::Poll(poll_fds, missing_source ? 200 : -1);
    CF_EXPECT_GE(poll_res, 0);

    if (stop_eventfd->IsOpen() && (poll_fds[1].revents & POLLIN)) {
      // Stop requested via eventfd
      eventfd_t val;
      stop_eventfd->EventfdRead(&val);
      return {};
    }

    if (poll_fds[0].revents & POLLIN) {
      uint32_t events = DrainInotifyEvents(dir_inotify_fd).value_or(IN_DELETE);
      if ((events & IN_DELETE) > 0) {
        launcher_monitor_source.reset();
        kernel_monitor_source.reset();
        logcat_monitor_source.reset();
      }
    }

    if (missing_source) {
      launcher_monitor_source.reset();  // will cut over to launcher.log soon
    }
  }
  return {};
}

}  // namespace cuttlefish
