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

#include "host/commands/cvd/cache/cache.h"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <android-base/strings.h>
#include <fmt/format.h>
#include <json/json.h>

#include "common/libs/utils/files.h"
#include "common/libs/utils/result.h"

namespace cuttlefish {

namespace {

Result<std::vector<std::string>> CacheFilesDesc(
    const std::string& cache_directory) {
  std::vector<std::string> contents = CF_EXPECTF(
      DirectoryContentsPaths(cache_directory),
      "Failure retrieving contents of directory at \"{}\"", cache_directory);

  auto not_self_or_parent_directory = [](std::string_view filepath) {
    return !android::base::EndsWith(filepath, ".") &&
           !android::base::EndsWith(filepath, "..");
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

Result<std::string> EmptyCache(const std::string& cache_directory) {
  CF_EXPECT(EnsureDirectoryExists(cache_directory));
  CF_EXPECT(RecursivelyRemoveDirectory(cache_directory));
  CF_EXPECT(EnsureDirectoryExists(cache_directory));
  return fmt::format("Cache at \"{}\" has been emptied\n", cache_directory);
}

Result<std::string> GetCacheInfo(const std::string& cache_directory,
                                 const bool json_formatted) {
  CF_EXPECT(EnsureDirectoryExists(cache_directory));
  std::size_t cache_size = CF_EXPECT(GetDiskUsageGigabytes(cache_directory));
  if (json_formatted) {
    Json::Value json_output(Json::objectValue);
    json_output["path"] = cache_directory;
    json_output["size_in_GB"] = std::to_string(cache_size);
    return json_output.toStyledString();
  }
  return fmt::format("path:{}\nsize in GB:{}\n", cache_directory, cache_size);
}

Result<std::string> PruneCache(const std::string& cache_directory,
                               const std::size_t allowed_size_GB) {
  CF_EXPECT(EnsureDirectoryExists(cache_directory));
  std::size_t cache_size = CF_EXPECT(GetDiskUsageGigabytes(cache_directory));
  // Descending because elements are removed from the back
  std::vector<std::string> cache_files =
      CF_EXPECT(CacheFilesDesc(cache_directory));
  while (cache_size > allowed_size_GB) {
    CHECK(!cache_files.empty()) << fmt::format(
        "Cache size is {} of {}, but there are no more files for pruning.",
        cache_size, allowed_size_GB);

    std::string next = cache_files.back();
    cache_files.pop_back();
    LOG(DEBUG) << fmt::format("Deleting \"{}\" for prune", next);
    // handles removal of non-directory top-level files as well
    CF_EXPECT(RecursivelyRemoveDirectory(next));
    cache_size = CF_EXPECT(GetDiskUsageGigabytes(cache_directory));
  }
  return fmt::format("Cache at \"{}\": ~{}GB of {}GB max\n", cache_directory,
                     cache_size, allowed_size_GB);
}

}  // namespace cuttlefish