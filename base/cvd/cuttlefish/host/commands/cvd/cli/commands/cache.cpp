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

#include "cuttlefish/host/commands/cvd/cli/commands/cache.h"

#include <stddef.h>

#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <fmt/base.h>
#include <fmt/core.h>
#include <fmt/format.h>
#include <fmt/ranges.h>
#include <json/value.h>

#include "cuttlefish/common/libs/utils/flag_parser.h"
#include "cuttlefish/host/commands/cvd/cache/cache.h"
#include "cuttlefish/host/commands/cvd/cli/command_request.h"
#include "cuttlefish/host/commands/cvd/cli/commands/command_handler.h"
#include "cuttlefish/host/commands/cvd/cli/types.h"
#include "cuttlefish/host/commands/cvd/utils/common.h"
#include "cuttlefish/result/result.h"

namespace cuttlefish {

namespace {

constexpr char kSummaryHelpText[] = "Manage the files cached by cvd";

enum class Action {
  Empty,
  Info,
  Prune,
};

struct CacheArguments {
  Action action = Action::Info;
  size_t allowed_size_gb = kDefaultCacheSizeGb;
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
  flags.emplace_back(GflagsCompatFlag("allowed_size_gb", result.allowed_size_gb)
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
    case Action::Empty: {
      CF_EXPECTF(EmptyCache(cache_directory), "Error emptying cache at {}",
                 cache_directory);
      fmt::print("Cache at \"{}\" has been emptied\n", cache_directory);
      break;
    }
    case Action::Info: {
      const size_t cache_size =
          CF_EXPECTF(GetCacheSize(cache_directory),
                     "Error retrieving size of cache at {}", cache_directory);
      if (arguments.json_formatted) {
        Json::Value json_output(Json::objectValue);
        json_output["path"] = cache_directory;
        json_output["size_in_GB"] = std::to_string(cache_size);
        fmt::print("{}", json_output.toStyledString());
      } else {
        fmt::print("path:{}\nsize in GB:{}\n", cache_directory, cache_size);
      }
      break;
    }
    case Action::Prune: {
      const PruneResult result =
          CF_EXPECTF(PruneCache(cache_directory, arguments.allowed_size_gb),
                     "Error pruning cache at {} to {}GB", cache_directory,
                     arguments.allowed_size_gb);
      if (result.before > result.after) {
        fmt::print("Cache pruned from {}GB down to {}GB\n", result.before,
                   result.after);
      }
      fmt::print("Cache at \"{}\": ~{}GB of {}GB max\n", cache_directory,
                 result.after, arguments.allowed_size_gb);
      break;
    }
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
    cvd cache prune --allowed_size_gb=<n> - caps the cache at the given size

**Notes**:
    - info and prune round the cache size up to the nearest gigabyte
    - prune uses last modification time to remove oldest files first
)",
                     kDefaultCacheSizeGb);
}

}  // namespace

std::unique_ptr<CvdCommandHandler> NewCvdCacheCommandHandler() {
  return std::unique_ptr<CvdCommandHandler>(new CvdCacheCommandHandler());
}

}  // namespace cuttlefish
