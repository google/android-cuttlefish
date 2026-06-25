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
#include <stdio.h>
#include <sys/eventfd.h>
#include <sys/inotify.h>
#include <sys/poll.h>
#include <sys/types.h>
#include <unistd.h>

#include <chrono>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "absl/strings/str_cat.h"

#include "cuttlefish/ansi_codes/ansi_codes.h"
#include "cuttlefish/common/libs/fs/shared_fd.h"
#include "cuttlefish/common/libs/utils/files.h"
#include "cuttlefish/host/commands/cvd/cli/commands/monitor/display.h"
#include "cuttlefish/host/commands/cvd/cli/utils.h"
#include "cuttlefish/host/commands/cvd/instances/local_instance.h"
#include "cuttlefish/host/libs/log_names/log_names.h"
#include "cuttlefish/result/result.h"

namespace cuttlefish {
namespace {

void UpdateFileAndWatch(const SharedFD& inotify_fd, const std::string& path,
                        SharedFD& fd, int& watch) {
  if (!fd->IsOpen()) {
    if (FileExists(path)) {
      fd = SharedFD::Open(path, O_RDONLY);
    }
  }
  if (fd->IsOpen() && watch == -1) {
    watch = inotify_fd->InotifyAddWatch(path, IN_DELETE_SELF | IN_MODIFY);
  }
}

}  // namespace

void ClearLastNLines(int n) {
  if (n > 0) {
    // Move cursor up N lines and clear to end of screen
    std::cout << AnsiCursorUp(n) << kAnsiClearScreenAfterCursor << std::flush;
  }
}
Result<void> MonitorLogs(const LocalInstance& instance) {
  return MonitorLogs(instance, SharedFD());
}

Result<void> MonitorLogs(const LocalInstance& instance, SharedFD stop_eventfd) {
  CF_EXPECT(isatty(0), "The monitor command requires an interactive terminal.");

  const std::string kernel_log =
      absl::StrCat(instance.InstanceDirectory(), "/logs/", kLogNameKernel);
  const std::string launcher_log =
      absl::StrCat(instance.InstanceDirectory(), "/logs/", kLogNameLauncher);
  const std::string logcat =
      absl::StrCat(instance.InstanceDirectory(), "/logs/", kLogNameLogcat);
  const std::string assemble_log =
      absl::StrCat(instance.AssemblyDirectory(), "/", kLogNameAssembleCvd);
  bool using_assemble_log = true;

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
  const int dir_watch =
      inotify_fd->InotifyAddWatch(logs_dir, IN_CREATE | IN_DELETE);

  int kernel_watch = -1;
  int launcher_watch = -1;
  int logcat_watch = -1;
  std::chrono::steady_clock::time_point last_draw_time =
      std::chrono::steady_clock::time_point();

  size_t last_total_lines_drawn = 0;

  while (true) {
    if (using_assemble_log) {
      if (FileExists(launcher_log)) {
        if (launcher_watch != -1) {
          inotify_fd->InotifyRmWatch(launcher_watch);
          launcher_watch = -1;
        }
        launcher_fd = SharedFD::Open(launcher_log, O_RDONLY);
        if (launcher_fd->IsOpen()) {
          launcher_watch = inotify_fd->InotifyAddWatch(
              launcher_log, IN_DELETE_SELF | IN_MODIFY);
          using_assemble_log = false;
        }
      } else if (!launcher_fd->IsOpen()) {
        if (FileExists(assemble_log)) {
          launcher_fd = SharedFD::Open(assemble_log, O_RDONLY);
          if (launcher_fd->IsOpen()) {
            if (launcher_watch == -1) {
              launcher_watch = inotify_fd->InotifyAddWatch(
                  assemble_log, IN_DELETE_SELF | IN_MODIFY);
            }
          }
        }
      }
    } else {
      UpdateFileAndWatch(inotify_fd, launcher_log, launcher_fd, launcher_watch);
    }

    UpdateFileAndWatch(inotify_fd, kernel_log, kernel_fd, kernel_watch);
    UpdateFileAndWatch(inotify_fd, logcat, logcat_fd, logcat_watch);

    const Result<TerminalSize> term_size_result = GetTerminalSize();
    int width = 79;  // Default fallback width (80 - 1)
    if (term_size_result.ok()) {
      width = term_size_result->columns - 1;
    }
    LogMonitorDisplay display(width);

    bool logcat_ready = false;
    if (logcat_fd->IsOpen() && logcat_fd->LSeek(0, SEEK_END) > 0) {
      logcat_ready = true;
    }

    size_t total_content = 30;

    const std::string launcher_name =
        using_assemble_log ? kLogNameAssembleCvd : kLogNameLauncher;
    size_t launcher_lines = total_content;
    size_t kernel_lines = 0;
    size_t logcat_lines = 0;
    if (kernel_fd->IsOpen()) {
      launcher_lines = total_content / 3;
      kernel_lines = total_content - launcher_lines;
    }
    if (logcat_ready) {
      kernel_lines = total_content / 3;
      logcat_lines = total_content - launcher_lines - kernel_lines;
    }
    display.DrawFile(launcher_fd, launcher_name, launcher_lines);
    display.DrawFile(kernel_fd, kLogNameKernel, kernel_lines);
    display.DrawFile(logcat_fd, kLogNameLogcat, logcat_lines);

    const auto [output, total_lines_drawn] = display.Finalize();
    ClearLastNLines(last_total_lines_drawn);
    std::cout << output << std::flush;
    last_total_lines_drawn = total_lines_drawn;

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
        PollSharedFd{.fd = inotify_fd, .events = POLLIN, .revents = 0},
    };
    if (stop_eventfd->IsOpen()) {
      poll_fds.push_back(
          PollSharedFd{.fd = stop_eventfd, .events = POLLIN, .revents = 0});
    }

    // Block until file changes occur. If any watch failed or is missing, use
    // a fallback timeout to awake and retry.
    int timeout_ms = -1;
    if (dir_watch == -1 || kernel_watch == -1 || launcher_watch == -1 ||
        logcat_watch == -1 || !logcat_ready || using_assemble_log) {
      timeout_ms = 200;
    }

    int poll_res = SharedFD::Poll(poll_fds, timeout_ms);

    if (stop_eventfd->IsOpen() && (poll_fds[1].revents & POLLIN)) {
      // Stop requested via eventfd
      eventfd_t val;
      stop_eventfd->EventfdRead(&val);
      ClearLastNLines(total_lines_drawn);
      return {};
    }

    if (poll_res > 0 && (poll_fds[0].revents & POLLIN)) {
      // Exhaustively drain all available events from the non-blocking
      // descriptor to coalesce rapid file modifications.
      char buf[4096]
          __attribute__((aligned(__alignof__(struct inotify_event))));
      ssize_t read_res = 0;
      while ((read_res = inotify_fd->Read(buf, sizeof(buf))) > 0) {
        char* ptr = buf;
        while (ptr < buf + read_res) {
          inotify_event* event = reinterpret_cast<inotify_event*>(ptr);
          if (event->mask & (IN_DELETE | IN_DELETE_SELF | IN_IGNORED)) {
            using_assemble_log = true;
            if (launcher_watch != -1) {
              inotify_fd->InotifyRmWatch(launcher_watch);
              launcher_watch = -1;
            }
            launcher_fd = SharedFD();
            if (kernel_watch != -1) {
              inotify_fd->InotifyRmWatch(kernel_watch);
              kernel_watch = -1;
            }
            kernel_fd = SharedFD();
            if (logcat_watch != -1) {
              inotify_fd->InotifyRmWatch(logcat_watch);
              logcat_watch = -1;
            }
            logcat_fd = SharedFD();
          }
          ptr += sizeof(inotify_event) + event->len;
        }
      }
      CF_EXPECT(read_res != 0,
                "Unexpected End-of-File reading inotify descriptor");
      const int err = inotify_fd->GetErrno();
      CF_EXPECTF(err == EAGAIN || err == EWOULDBLOCK,
                 "Unexpected error reading inotify descriptor: {} ({})",
                 inotify_fd->StrError(), err);
    }
  }

  return {};
}

}  // namespace cuttlefish
