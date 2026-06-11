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

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <sys/inotify.h>
#include <sys/types.h>
#include <unistd.h>

#include <chrono>
#include <iostream>
#include <memory>
#include <string>
#include <thread>

#include "absl/strings/str_cat.h"

#include "cuttlefish/common/libs/fs/shared_fd.h"
#include "cuttlefish/host/commands/cvd/cli/commands/monitor/ansi_codes.h"
#include "cuttlefish/host/commands/cvd/cli/commands/monitor/display.h"
#include "cuttlefish/host/commands/cvd/cli/utils.h"
#include "cuttlefish/host/commands/cvd/instances/local_instance.h"
#include "cuttlefish/host/libs/log_names/log_names.h"
#include "cuttlefish/result/result.h"

namespace cuttlefish {

namespace {

void ClearLastNLines(int n) {
  if (n > 0) {
    // Move cursor up N lines and clear to end of screen
    std::cout << AnsiCursorUp(n) << kAnsiClearScreenAfterCursor << std::flush;
  }
}

void UpdateFileAndWatch(const SharedFD& inotify_fd, const std::string& path,
                        SharedFD& fd, int& watch) {
  if (!fd->IsOpen()) {
    fd = SharedFD::Open(path, O_RDONLY);
  }
  if (fd->IsOpen() && watch == -1) {
    watch = inotify_fd->InotifyAddWatch(path, IN_MODIFY);
  }
}

}  // namespace

Result<void> MonitorLogs(const LocalInstance& instance) {
  CF_EXPECT(isatty(0), "The monitor command requires an interactive terminal.");

  const std::string kernel_log =
      absl::StrCat(instance.InstanceDirectory(), "/logs/", kLogNameKernel);
  const std::string launcher_log =
      absl::StrCat(instance.InstanceDirectory(), "/logs/", kLogNameLauncher);
  const std::string logcat =
      absl::StrCat(instance.InstanceDirectory(), "/logs/", kLogNameLogcat);

  SharedFD kernel_fd;
  SharedFD launcher_fd;
  SharedFD logcat_fd;

  const SharedFD inotify_fd = SharedFD::InotifyFd();
  CF_EXPECT(inotify_fd->IsOpen(), "Failed to create inotify fd");
  const int flags = inotify_fd->Fcntl(F_GETFL, 0);
  CF_EXPECT(inotify_fd->Fcntl(F_SETFL, flags | O_NONBLOCK) != -1,
            "Failed to set inotify fd to non-blocking");

  const std::string logs_dir =
      absl::StrCat(instance.InstanceDirectory(), "/logs");
  const int dir_watch = inotify_fd->InotifyAddWatch(logs_dir, IN_CREATE);

  int kernel_watch = -1;
  int launcher_watch = -1;
  int logcat_watch = -1;
  std::chrono::steady_clock::time_point last_draw_time =
      std::chrono::steady_clock::time_point();

  while (true) {
    UpdateFileAndWatch(inotify_fd, kernel_log, kernel_fd, kernel_watch);
    UpdateFileAndWatch(inotify_fd, launcher_log, launcher_fd, launcher_watch);
    UpdateFileAndWatch(inotify_fd, logcat, logcat_fd, logcat_watch);

    const Result<TerminalSize> term_size_result = GetTerminalSize();
    int width = 79;  // Default fallback width (80 - 1)
    if (term_size_result.ok()) {
      width = term_size_result->columns - 1;
    }
    LogMonitorDisplay display(width);

    display.DrawFile(launcher_fd, kLogNameLauncher);
    display.DrawFile(kernel_fd, kLogNameKernel);
    display.DrawFile(logcat_fd, kLogNameLogcat);

    const auto [output, total_lines_drawn] = display.Finalize();
    std::cout << output << std::flush;
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

    // Block until file changes occur. If any watch failed or is missing, use
    // a 1-second fallback timeout to awake and retry.
    // NOLINTNEXTLINE(misc-include-cleaner)
    PollSharedFd poll_fd = {inotify_fd, POLLIN, 0};
    int timeout_ms = -1;
    if (dir_watch == -1 || kernel_watch == -1 || launcher_watch == -1 ||
        logcat_watch == -1) {
      timeout_ms = 1000;
    }

    // NOLINTNEXTLINE(misc-include-cleaner)
    if (SharedFD::Poll(&poll_fd, 1, timeout_ms) > 0 &&
        (poll_fd.revents & POLLIN)) {
      // Exhaustively drain all available events from the non-blocking
      // descriptor to coalesce rapid file modifications.
      char buf[4096]
          __attribute__((aligned(__alignof__(struct inotify_event))));
      ssize_t read_res = 0;
      while ((read_res = inotify_fd->Read(buf, sizeof(buf))) > 0) {
      }
      CF_EXPECT(read_res != 0,
                "Unexpected End-of-File reading inotify descriptor");
      const int err = inotify_fd->GetErrno();
      CF_EXPECTF(err == EAGAIN || err == EWOULDBLOCK,
                 "Unexpected error reading inotify descriptor: {} ({})",
                 inotify_fd->StrError(), err);
    }
    ClearLastNLines(total_lines_drawn);
  }

  return {};
}

}  // namespace cuttlefish
