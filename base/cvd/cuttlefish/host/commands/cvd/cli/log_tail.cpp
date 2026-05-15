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

#include "cuttlefish/host/commands/cvd/cli/log_tail.h"

#include <algorithm>
#include <cstddef>
#include <string>
#include <utility>
#include <vector>

#include "absl/strings/cord.h"
#include "absl/strings/str_split.h"

#include "cuttlefish/common/libs/fs/shared_buf.h"
#include "cuttlefish/common/libs/fs/shared_fd.h"
#include "cuttlefish/result/result.h"

namespace cuttlefish {

Result<std::vector<std::string>> GetLastNLines(SharedFD fd, size_t n) {
  off_t file_size = fd->LSeek(0, SEEK_END);
  CF_EXPECT(file_size != -1, "Failed to seek to end of file");

  absl::Cord accumulated_data;
  off_t offset = file_size;
  size_t newline_count = 0;

  while (offset > 0 && newline_count < n + 1) {
    static constexpr off_t kChunkSize = 4096;
    size_t to_read = std::min(kChunkSize, offset);
    offset -= to_read;
    fd->LSeek(offset, SEEK_SET);

    std::string chunk(to_read, '\0');
    ssize_t bytes_read = ReadExact(fd, &chunk);
    CF_EXPECTF(bytes_read == static_cast<ssize_t>(to_read), "Read failed: '{}'",
               fd->StrError());

    newline_count += std::count(chunk.begin(), chunk.end(), '\n');
    accumulated_data.Prepend(std::move(chunk));
  }

  std::vector<std::string> all_lines =
      absl::StrSplit(std::string(accumulated_data), '\n');

  if (!all_lines.empty() && all_lines.back().empty()) {
    all_lines.pop_back();
  }

  size_t start_idx = all_lines.size() > n ? all_lines.size() - n : 0;
  all_lines.erase(all_lines.begin(), all_lines.begin() + start_idx);

  return all_lines;
}

}  // namespace cuttlefish
