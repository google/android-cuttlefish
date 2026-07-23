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

#include "cuttlefish/host/commands/cvd/cli/commands/monitor/file_monitor_source.h"

#include <fcntl.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/inotify.h>

#include <algorithm>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/log/check.h"
#include "absl/strings/cord.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_split.h"
#include "android-base/file.h"

#include "cuttlefish/common/libs/fs/shared_fd.h"
#include "cuttlefish/host/commands/cvd/cli/commands/monitor/drain_inotify.h"
#include "cuttlefish/host/commands/cvd/cli/commands/monitor/monitor_source.h"
#include "cuttlefish/host/commands/cvd/cli/format_byte_size.h"
#include "cuttlefish/io/io.h"
#include "cuttlefish/io/read_exact.h"
#include "cuttlefish/result/expect.h"
#include "cuttlefish/result/result_type.h"

namespace cuttlefish {
namespace {

struct FileData {
  std::vector<std::string> last_lines;
  size_t total_size = 0;
};

Result<FileData> GetLastNLines(ReaderSeeker& rs, size_t n) {
  FileData ret;
  ret.total_size = CF_EXPECT(rs.SeekEnd(0));

  absl::Cord accumulated_data;
  size_t offset = ret.total_size;
  size_t newline_count = 0;

  while (offset > 0 && newline_count < n + 1) {
    static constexpr size_t kChunkSize = 4096;
    size_t to_read = std::min(kChunkSize, offset);
    offset -= to_read;
    CF_EXPECT(rs.SeekSet(offset));

    std::string chunk(to_read, '\0');
    CF_EXPECT(ReadExact(rs, chunk.data(), to_read));

    newline_count += std::count(chunk.begin(), chunk.end(), '\n');
    accumulated_data.Prepend(std::move(chunk));
  }

  ret.last_lines = absl::StrSplit(std::string(accumulated_data), '\n');

  // Handle trailing newline
  if (!ret.last_lines.empty() && ret.last_lines.back().empty()) {
    ret.last_lines.pop_back();
  }

  size_t start_idx = ret.last_lines.size() > n ? ret.last_lines.size() - n : 0;
  ret.last_lines.erase(ret.last_lines.begin(),
                       ret.last_lines.begin() + start_idx);

  return ret;
}

}  // namespace

FileMonitorSource::FileMonitorSource(
    std::string path, std::unique_ptr<ReaderSeeker> file_io,
    std::function<Result<std::string>(std::string_view)> colorize_line)
    : path_(std::move(path)),
      file_io_(std::move(file_io)),
      colorize_line_(std::move(colorize_line)) {
  inotify_fd_ = SharedFD::InotifyFd();
  CHECK(inotify_fd_->IsOpen()) << inotify_fd_->StrError();
  CHECK_GE(inotify_fd_->InotifyAddWatch(path_, IN_DELETE_SELF | IN_MODIFY), 0);
  const int flags = inotify_fd_->Fcntl(F_GETFL, 0);
  CHECK_GE(inotify_fd_->Fcntl(F_SETFL, flags | O_NONBLOCK), 0);
}

FileMonitorSource::~FileMonitorSource() = default;

MonitorOutput FileMonitorSource::Report(size_t rows, size_t) {
  const std::string basename = android::base::Basename(path_);
  Result<FileData> file_data = GetLastNLines(*file_io_, rows);
  if (!file_data.has_value()) {
    return MonitorOutput(
        absl::StrCat(basename, " (error)"),
        absl::StrSplit(file_data.error().FormatForEnv(true), '\n'));
  }
  for (std::string& line : file_data->last_lines) {
    line = colorize_line_(line).value_or(line);
  }
  std::string size = FormatByteSize(file_data->total_size);
  Result<uint32_t> unused = DrainInotifyEvents(inotify_fd_);
  return MonitorOutput(absl::StrCat(basename, " (", size, ")"),
                       file_data->last_lines);
}

SharedFD FileMonitorSource::ReadyFd() { return inotify_fd_; }

}  // namespace cuttlefish
