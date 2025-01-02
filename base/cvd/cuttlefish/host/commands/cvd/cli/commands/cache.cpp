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
#include <chrono>
#include <cstddef>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include <android-base/logging.h>
#include <android-base/strings.h>
#include <fmt/format.h>
#include <json/json.h>

#include "common/libs/utils/files.h"
#include "common/libs/utils/flag_parser.h"
#include "common/libs/utils/result.h"
#include "common/libs/utils/tee_logging.h"
#include "host/commands/cvd/cli/commands/command_handler.h"
#include "host/commands/cvd/cli/types.h"
#include "host/commands/cvd/utils/common.h"

namespace cuttlefish {

namespace {
constexpr int kDefaultCacheSizeGB = 25;

constexpr char kSummaryHelpText[] = "Manage the files cached by cvd";

enum class Action {
  Empty,
  Info,
  Prune,
};

struct CacheArguments {
  Action action = Action::Info;
  std::size_t allowed_size_GB = kDefaultCacheSizeGB;
  bool json_formatted = false;
};

Result<Action> ToAction(std::string_view key) {
  if (key == "empty") {
    return Action::Empty;
  } else if (key == "info") {
    return Action::Info;
  } else if (key == "prune") {
    return Action::Prune;
  }
  return CF_ERRF("Unable to determine action \"{}\"", key);
}

Result<CacheArguments> ProcessArguments(
    const std::vector<std::string>& subcommand_arguments) {
  if (subcommand_arguments.empty()) {
    return CacheArguments{};
  }
  std::vector<std::string> cache_arguments = subcommand_arguments;
  std::string action = cache_arguments.front();
  cache_arguments.erase(cache_arguments.begin());
  CacheArguments result{
      .action = CF_EXPECTF(ToAction(action),
                           "Provided \"{}\" is not a valid cache action.  (Is "
                           "there a non-selector flag before the action?)",
                           action),
  };

  std::vector<Flag> flags;
  flags.emplace_back(GflagsCompatFlag("allowed_size_GB", result.allowed_size_GB)
                         .Help("Allowed size of the cache during prune "
                               "operation, in gigabytes."));
  flags.emplace_back(GflagsCompatFlag("json", result.json_formatted)
                         .Help("Output `info` command in JSON format."));
  flags.emplace_back(UnexpectedArgumentGuard());
  CF_EXPECTF(ConsumeFlags(flags, cache_arguments),
             "Failure processing arguments and flags: cvd cache {} {}", action,
             fmt::join(cache_arguments, " "));

  return result;
}

Result<std::string> RunEmpty(const std::string& cache_directory) {
  CF_EXPECT(RecursivelyRemoveDirectory(cache_directory));
  CF_EXPECT(EnsureDirectoryExists(cache_directory));
  return fmt::format("Cache at \"{}\" has been emptied\n", cache_directory);
}

Result<std::string> RunInfo(const std::string& cache_directory,
                            const bool json_formatted) {
  std::size_t cache_size = CF_EXPECT(GetDiskUsageGigabytes(cache_directory));
  if (json_formatted) {
    Json::Value json_output(Json::objectValue);
    json_output["path"] = cache_directory;
    json_output["size_in_GB"] = std::to_string(cache_size);
    return json_output.toStyledString();
  }
  return fmt::format("path:{}\nsize in GB:{}\n", cache_directory, cache_size);
}

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

Result<std::string> RunPrune(const std::string& cache_directory,
                             const std::size_t allowed_size_GB) {
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

class CvdCacheCommandHandler : public CvdCommandHandler {
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
  std::string cache_directory = PerUserCacheDir();
  CF_EXPECT(EnsureDirectoryExists(cache_directory));
  switch (arguments.action) {
    case Action::Empty:
      std::cout << CF_EXPECTF(RunEmpty(cache_directory),
                              "Error emptying cache at {}", cache_directory);
      break;
    case Action::Info:
      std::cout << CF_EXPECTF(
          RunInfo(cache_directory, arguments.json_formatted),
          "Error retrieving info of cache at {}", cache_directory);
      break;
    case Action::Prune:
      std::cout << CF_EXPECTF(
          RunPrune(cache_directory, arguments.allowed_size_GB),
          "Error pruning cache at {} to {}GB", cache_directory,
          arguments.allowed_size_GB);
      break;
  }

  return {};
}

Result<std::string> CvdCacheCommandHandler::SummaryHelp() const {
  return kSummaryHelpText;
}

Result<std::string> CvdCacheCommandHandler::DetailedHelp(
    std::vector<std::string>&) const {
  return fmt::format(R"(usage: cvd cache <action> [<flag>...]

Example usage:
    cvd cache empty - wipes out all files in the cache directory

    cvd cache info - display the filepath and approximate size of the cache
    cvd cache info --json - the same as above, but in JSON format

    cvd cache prune - caps the cache at the default size ({}GB)
    cvd cache prune --allowed_size_GB=<n> - caps the cache at the given size

**Notes**:
    - info and prune round the cache size up to the nearest gigabyte
    - prune uses last modification time to remove oldest files first
)",
                     kDefaultCacheSizeGB);
}

}  // namespace

std::unique_ptr<CvdCommandHandler> NewCvdCacheCommandHandler() {
  return std::unique_ptr<CvdCommandHandler>(new CvdCacheCommandHandler());
}

}  // namespace cuttlefish
