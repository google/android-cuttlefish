/*
 * Copyright (C) 2024 The Android Open Source Project
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

#include "host/commands/cvd/cli/commands/cache.h"

#include <algorithm>
#include <cstddef>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <android-base/logging.h>
#include <fmt/format.h>

#include "common/libs/utils/files.h"
#include "common/libs/utils/flag_parser.h"
#include "common/libs/utils/result.h"
#include "common/libs/utils/tee_logging.h"
#include "host/commands/cvd/cli/commands/server_handler.h"
#include "host/commands/cvd/cli/types.h"
#include "host/commands/cvd/utils/common.h"

namespace cuttlefish {

namespace {
constexpr int kDefaultCacheSizeGigabytes = 25;

constexpr char kDetailedHelpText[] =
    R"(usage: cvd cache <subcommand> 

Example usage:
    cvd cache where - display the filepath of the cache
    cvd cache size - display the approximate size of the cache
    cvd cache cleanup - caps the cache at the default size
    cvd cache cleanup --allowed_size_GB=<n> - caps the cache at the given size
    cvd cache empty - wipes out all files in the cache directory

**Notes**:
    - size and cleanup round the cache size up to the nearest gigabyte for calculating
    - cleanup uses last modification time to remove oldest files first
)";

constexpr char kSummaryHelpText[] = "Used to manage the files cached by cvd";

enum class CacheSubcommand {
  Cleanup,
  Empty,
  Size,
  Where,
};

struct CacheArguments {
  CacheSubcommand subcommand = CacheSubcommand::Where;
  std::size_t allowed_size_GB = kDefaultCacheSizeGigabytes;
};

std::string GetCacheDirectory() {
  const std::string cache_base_path = PerUserDir() + "/cache";
  return cache_base_path;
}

Result<CacheSubcommand> ToCacheSubcommand(const std::string& key) {
  const std::unordered_map<std::string, CacheSubcommand> kString_mapping{
      {"cleanup", CacheSubcommand::Cleanup},
      {"empty", CacheSubcommand::Empty},
      {"size", CacheSubcommand::Size},
      {"where", CacheSubcommand::Where},
  };
  auto lookup = kString_mapping.find(key);
  CF_EXPECT(lookup != kString_mapping.end(), "Unable to find subcommand");
  return lookup->second;
}

Result<CacheArguments> ProcessArguments(
    const std::vector<std::string>& subcommand_arguments) {
  CF_EXPECT(subcommand_arguments.empty() == false,
            "cvd cache requires at least a subcommand argument.  Run `cvd help "
            "cache` for details.");
  std::vector<std::string> cache_arguments = subcommand_arguments;
  std::string subcommand = cache_arguments.front();
  cache_arguments.erase(cache_arguments.begin());
  CacheArguments result{
      .subcommand =
          CF_EXPECTF(ToCacheSubcommand(subcommand),
                     "Provided \"{}\" is not a valid cache subcommand.  (Is "
                     "there a non-selector flag before the subcommand?)",
                     subcommand),
  };

  std::vector<Flag> flags;
  flags.emplace_back(GflagsCompatFlag("allowed_size_GB", result.allowed_size_GB)
                         .Help("Allowed size of the cache during cleanup "
                               "operation, in gigabytes."));
  flags.emplace_back(UnexpectedArgumentGuard());
  CF_EXPECT(ConsumeFlags(flags, cache_arguments));
  CF_EXPECTF(ConsumeFlags(flags, cache_arguments),
             "Failure processing arguments and flags: cvd {} {}", subcommand,
             fmt::join(cache_arguments, " "));

  return result;
}

// TODO CJR: this could live generically in files.h/cpp
Result<std::vector<std::string>> GetFilesByModTimeDescending(
    const std::string& cache_directory) {
  std::vector<std::string> cache_files = CF_EXPECTF(
      DirectoryContents(cache_directory),
      "Failure retrieving cache file list at \"{}\"", cache_directory);
  std::vector<std::string> filtered;
  std::copy_if(
      cache_files.begin(), cache_files.end(), std::back_inserter(filtered),
      [](std::string filename) { return filename != "." && filename != ".."; });
  std::sort(filtered.begin(), filtered.end(), [](std::string a, std::string b) {
    return FileModificationTime(a) > FileModificationTime(b);
  });
  return filtered;
}

Result<void> RunCleanup(const std::string& cache_directory,
                        const std::size_t allowed_size_GB) {
  std::size_t cache_size = CF_EXPECT(GetDiskUsageGigabytes(cache_directory));
  // Descending because elements are removed from the back
  std::vector<std::string> cache_files =
      CF_EXPECT(GetFilesByModTimeDescending(cache_directory));
  std::string next;
  while (cache_size > allowed_size_GB) {
    // assuming all files in the cache are directories
    // TODO CJR: test what happens if that is untrue
    CF_EXPECT(cache_files.empty() == false,
              "Error removing files for cleanup, no more files found.");
    next = cache_files.back();
    cache_files.pop_back();
    LOG(DEBUG) << fmt::format("Deleting \"{}\" for cleanup", next);
    CF_EXPECT(RecursivelyRemoveDirectory(
        fmt::format("{}/{}", cache_directory, next)));
    cache_size = CF_EXPECT(GetDiskUsageGigabytes(cache_directory));
  }
  LOG(INFO) << fmt::format("Cache at \"{}\": ~{}/{}GB", cache_directory,
                           cache_size, allowed_size_GB);
  return {};
}

Result<void> RunEmpty(const std::string& cache_directory) {
  CF_EXPECT(RecursivelyRemoveDirectory(cache_directory));
  CF_EXPECT(EnsureDirectoryExists(cache_directory));
  LOG(INFO) << fmt::format("Cache at \"{}\" has been emptied", cache_directory);
  return {};
}

Result<void> RunSize(const std::string& cache_directory) {
  std::size_t cache_size = CF_EXPECT(GetDiskUsageGigabytes(cache_directory));
  LOG(INFO) << fmt::format("Cache at \"{}\": ~{}GB", cache_directory,
                           cache_size);
  return {};
}

void RunWhere(std::string_view cache_directory) {
  LOG(INFO) << fmt::format("Cache located at: {}", cache_directory);
}

class CvdCacheCommandHandler : public CvdServerHandler {
 public:
  Result<void> Handle(const CommandRequest& request) override;
  cvd_common::Args CmdList() const override { return {"cache"}; }
  Result<std::string> SummaryHelp() const override;
  bool ShouldInterceptHelp() const override { return true; }
  Result<std::string> DetailedHelp(std::vector<std::string>&) const override;
};

Result<void> CvdCacheCommandHandler::Handle(const CommandRequest& request) {
  CF_EXPECT(CanHandle(request));
  auto logger = ScopedTeeLogger(LogToStderr());

  CacheArguments arguments =
      CF_EXPECT(ProcessArguments(request.SubcommandArguments()));
  std::string cache_directory = GetCacheDirectory();
  switch (arguments.subcommand) {
    case CacheSubcommand::Cleanup:
      CF_EXPECTF(RunCleanup(cache_directory, arguments.allowed_size_GB),
                 "Unable to clean up cache at {}", cache_directory);
      break;
    case CacheSubcommand::Empty:
      CF_EXPECTF(RunEmpty(cache_directory), "Unable to clean up cache at {}",
                 cache_directory);
      break;
    case CacheSubcommand::Size:
      CF_EXPECTF(RunSize(cache_directory), "Unable to clean up cache at {}",
                 cache_directory);
      break;
    case CacheSubcommand::Where:
      RunWhere(cache_directory);
      break;
  }

  return {};
}

Result<std::string> CvdCacheCommandHandler::SummaryHelp() const {
  return kSummaryHelpText;
}

Result<std::string> CvdCacheCommandHandler::DetailedHelp(
    std::vector<std::string>&) const {
  return kDetailedHelpText;
}

}  // namespace

std::unique_ptr<CvdServerHandler> NewCvdCacheCommandHandler() {
  return std::unique_ptr<CvdServerHandler>(new CvdCacheCommandHandler());
}

}  // namespace cuttlefish
