/*
 * Copyright (C) 2025 The Android Open Source Project
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

#include "cuttlefish/host/commands/cvd/cache/cache.h"

#include <stddef.h>

#include <algorithm>
#include <chrono>
#include <iterator>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <fmt/format.h>
#include "absl/strings/match.h"
#include "absl/log/check.h"
#include "absl/log/log.h"

#include "cuttlefish/common/libs/utils/disk_usage.h"
#include "cuttlefish/common/libs/utils/files.h"
#include "cuttlefish/result/result.h"

namespace cuttlefish {

namespace {

Result<std::vector<std::string>> CacheFilesDesc(
    const std::string& cache_directory) {
  std::vector<std::string> contents = CF_EXPECTF(
      DirectoryContentsPaths(cache_directory),
      "Failure retrieving contents of directory at \"{}\"", cache_directory);

  auto not_self_or_parent_directory = [](std::string_view filepath) {
    return !absl::EndsWith(filepath, ".") && !absl::EndsWith(filepath, "..");
  };
  std::vector<std::string> filtered;
  std::copy_if(contents.begin(), contents.end(), std::back_inserter(filtered),
               not_self_or_parent_directory);

  using ModTimePair =
      std::pair<std::string, std::chrono::system_clock::time_point>;
  std::vector<ModTimePair> to_sort;
  for (const std::string& filename : filtered) {
    to_sort.emplace_back(
        std::pair(filename, CF_EXPECT(FileModificationTime(filename))));
  }
  std::sort(to_sort.begin(), to_sort.end(),
            [](const ModTimePair& a, const ModTimePair& b) {
              return a.second > b.second;
            });

  std::vector<std::string> result;
  for (const ModTimePair& pair : to_sort) {
    result.emplace_back(pair.first);
  }
  return result;
}

}  // namespace

Result<void> EmptyCache(const std::string& cache_directory) {
  CF_EXPECT(EnsureDirectoryExists(cache_directory));
  CF_EXPECT(RecursivelyRemoveDirectory(cache_directory));
  CF_EXPECT(EnsureDirectoryExists(cache_directory));
  return {};
}

Result<size_t> GetCacheSize(const std::string& cache_directory) {
  CF_EXPECT(EnsureDirectoryExists(cache_directory));
  return CF_EXPECT(GetDiskUsageGigabytes(cache_directory));
}

Result<PruneResult> PruneCache(const std::string& cache_directory,
                               const size_t allowed_size_gb) {
  CF_EXPECT(EnsureDirectoryExists(cache_directory));
  size_t cache_size = CF_EXPECT(GetDiskUsageGigabytes(cache_directory));
  PruneResult result{
      .before = cache_size,
  };
  // Descending because elements are removed from the back
  std::vector<std::string> cache_files =
      CF_EXPECT(CacheFilesDesc(cache_directory));
  while (cache_size > allowed_size_gb) {
    CHECK(!cache_files.empty()) << fmt::format(
        "Cache size is {} of {}, but there are no more files for pruning.",
        cache_size, allowed_size_gb);

    std::string next = cache_files.back();
    cache_files.pop_back();
    VLOG(0) << fmt::format("Deleting \"{}\" for prune", next);
    // handles removal of non-directory top-level files as well
    CF_EXPECT(RecursivelyRemoveDirectory(next));
    cache_size = CF_EXPECT(GetDiskUsageGigabytes(cache_directory));
  }
  result.after = cache_size;
  return result;
}

}  // namespace cuttlefish
