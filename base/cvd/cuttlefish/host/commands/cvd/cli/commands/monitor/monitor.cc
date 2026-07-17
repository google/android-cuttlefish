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
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include "absl/cleanup/cleanup.h"
#include "absl/strings/str_cat.h"

#include "cuttlefish/ansi_codes/ansi_codes.h"
#include "cuttlefish/common/libs/fs/shared_fd.h"
#include "cuttlefish/files/file_exists.h"
#include "cuttlefish/host/commands/cvd/cli/commands/monitor/display.h"
#include "cuttlefish/host/commands/cvd/cli/commands/monitor/file_monitor_source.h"
#include "cuttlefish/host/commands/cvd/cli/commands/monitor/kernel.h"
#include "cuttlefish/host/commands/cvd/cli/commands/monitor/launcher.h"
#include "cuttlefish/host/commands/cvd/cli/commands/monitor/log_tee.h"
#include "cuttlefish/host/commands/cvd/cli/commands/monitor/logcat.h"
#include "cuttlefish/host/commands/cvd/cli/commands/monitor/monitor_source.h"
#include "cuttlefish/host/commands/cvd/cli/utils.h"
#include "cuttlefish/host/commands/cvd/instances/local_instance.h"
#include "cuttlefish/host/libs/log_names/log_names.h"
#include "cuttlefish/io/io.h"
#include "cuttlefish/io/shared_fd.h"
#include "cuttlefish/result/result.h"

namespace cuttlefish {
namespace {

Result<std::string> ColorLauncherOrLogTee(std::string_view line) {
  if (line.find("log_tee(") != 0) {
    return CF_EXPECT(ColorLauncherLine(line));
  }
  const size_t bracket = line.find(']');
  if (bracket != std::string_view::npos) {
    line.remove_prefix(bracket + 1);
    const size_t first_non_space = line.find_first_not_of(" \t");
    if (first_non_space != std::string_view::npos) {
      line.remove_prefix(first_non_space);
    } else {
      line = "";
    }
  }
  return CF_EXPECT(ColorLogTeeLine(line));
}

// TODO(schuffelen): integrate inotify watches into MonitorSource

std::unique_ptr<MonitorSource> LauncherLogMonitorSource(
    const LocalInstance& instance, SharedFD inotify_fd, int& watch) {
  if (watch != -1) {
    inotify_fd->InotifyRmWatch(watch);
    watch = -1;
  }
  const std::string launcher =
      absl::StrCat(instance.InstanceDirectory(), "/logs/", kLogNameLauncher);
  const std::string assemble =
      absl::StrCat(instance.AssemblyDirectory(), "/", kLogNameAssembleCvd);
  const std::string path = FileExists(launcher) ? launcher : assemble;
  if (!FileExists(path)) {
    return {};
  }
  SharedFD fd = SharedFD::Open(path, O_RDONLY);
  if (!fd->IsOpen() || fd->LSeek(0, SEEK_END) <= 0) {
    return {};
  }
  watch = inotify_fd->InotifyAddWatch(path, IN_DELETE_SELF | IN_MODIFY);
  std::unique_ptr<ReaderSeeker> io = std::make_unique<SharedFdIo>(fd);
  return std::make_unique<FileMonitorSource>(path, std::move(io),
                                             ColorLauncherOrLogTee);
}

std::unique_ptr<MonitorSource> KernelLogMonitorSource(
    const LocalInstance& instance, SharedFD inotify_fd, int& watch) {
  if (watch != -1) {
    inotify_fd->InotifyRmWatch(watch);
    watch = -1;
  }
  const std::string path =
      absl::StrCat(instance.InstanceDirectory(), "/logs/", kLogNameKernel);
  if (!FileExists(path)) {
    return {};
  }
  SharedFD fd = SharedFD::Open(path, O_RDONLY);
  if (!fd->IsOpen() || fd->LSeek(0, SEEK_END) <= 0) {
    return {};
  }
  watch = inotify_fd->InotifyAddWatch(path, IN_DELETE_SELF | IN_MODIFY);
  std::unique_ptr<ReaderSeeker> io = std::make_unique<SharedFdIo>(fd);
  return std::make_unique<FileMonitorSource>(path, std::move(io),
                                             ColorKernelLine);
}

std::unique_ptr<MonitorSource> LogcatMonitorSource(
    const LocalInstance& instance, SharedFD inotify_fd, int& watch) {
  if (watch != -1) {
    inotify_fd->InotifyRmWatch(watch);
    watch = -1;
  }
  const std::string path =
      absl::StrCat(instance.InstanceDirectory(), "/logs/", kLogNameLogcat);
  if (!FileExists(path)) {
    return {};
  }
  SharedFD fd = SharedFD::Open(path, O_RDONLY);
  if (!fd->IsOpen() || fd->LSeek(0, SEEK_END) <= 0) {
    return {};
  }
  watch = inotify_fd->InotifyAddWatch(path, IN_DELETE_SELF | IN_MODIFY);
  std::unique_ptr<ReaderSeeker> io = std::make_unique<SharedFdIo>(fd);
  return std::make_unique<FileMonitorSource>(path, std::move(io),
                                             ColorLogcatLine);
}

}  // namespace

Result<void> MonitorLogs(const LocalInstance& instance, SharedFD stop_eventfd) {
  CF_EXPECT(isatty(0), "The monitor command requires an interactive terminal.");

  std::unique_ptr<MonitorSource> kernel_monitor_source;
  std::unique_ptr<MonitorSource> launcher_monitor_source;
  std::unique_ptr<MonitorSource> logcat_monitor_source;

  const SharedFD inotify_fd = SharedFD::InotifyFd();
  CF_EXPECT(inotify_fd->IsOpen(), "Failed to create inotify fd");
  const int flags = inotify_fd->Fcntl(F_GETFL, 0);
  CF_EXPECT(inotify_fd->Fcntl(F_SETFL, flags | O_NONBLOCK) != -1,
            "Failed to set inotify fd to non-blocking");

  const std::string logs_dir =
      absl::StrCat(instance.InstanceDirectory(), "/logs");
  inotify_fd->InotifyAddWatch(logs_dir, IN_CREATE | IN_DELETE);

  int kernel_watch = -1;
  int launcher_watch = -1;
  int logcat_watch = -1;
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
      kernel_monitor_source =
          KernelLogMonitorSource(instance, inotify_fd, kernel_watch);
    }
    if (!logcat_monitor_source.get()) {
      logcat_monitor_source =
          LogcatMonitorSource(instance, inotify_fd, logcat_watch);
    }
    if (!launcher_monitor_source.get()) {
      launcher_monitor_source =
          LauncherLogMonitorSource(instance, inotify_fd, launcher_watch);
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

    if (missing_source) {
      launcher_monitor_source.reset();  // will cut over to launcher.log soon
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
        PollSharedFd{.fd = inotify_fd, .events = POLLIN, .revents = 0},
    };
    if (stop_eventfd->IsOpen()) {
      poll_fds.push_back(
          PollSharedFd{.fd = stop_eventfd, .events = POLLIN, .revents = 0});
    }

    // Block until file changes occur. If any watch failed or is missing, use
    // a fallback timeout to awake and retry.
    int poll_res = SharedFD::Poll(poll_fds, missing_source ? 200 : -1);

    if (stop_eventfd->IsOpen() && (poll_fds[1].revents & POLLIN)) {
      // Stop requested via eventfd
      eventfd_t val;
      stop_eventfd->EventfdRead(&val);
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
            if (launcher_watch != -1) {
              inotify_fd->InotifyRmWatch(launcher_watch);
              launcher_watch = -1;
            }
            launcher_monitor_source.reset();
            if (kernel_watch != -1) {
              inotify_fd->InotifyRmWatch(kernel_watch);
              kernel_watch = -1;
            }
            kernel_monitor_source.reset();
            if (logcat_watch != -1) {
              inotify_fd->InotifyRmWatch(logcat_watch);
              logcat_watch = -1;
            }
            logcat_monitor_source.reset();
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
