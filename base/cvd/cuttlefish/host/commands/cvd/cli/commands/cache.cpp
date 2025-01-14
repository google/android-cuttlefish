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
#include "host/commands/cvd/cache/cache.h"
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

  CacheArguments arguments =
      CF_EXPECT(ProcessArguments(request.SubcommandArguments()));
  std::string cache_directory = PerUserCacheDir();
  switch (arguments.action) {
    case Action::Empty:
      std::cout << CF_EXPECTF(EmptyCache(cache_directory),
                              "Error emptying cache at {}", cache_directory);
      break;
    case Action::Info:
      std::cout << CF_EXPECTF(
          GetCacheInfo(cache_directory, arguments.json_formatted),
          "Error retrieving info of cache at {}", cache_directory);
      break;
    case Action::Prune:
      std::cout << CF_EXPECTF(
          PruneCache(cache_directory, arguments.allowed_size_GB),
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
