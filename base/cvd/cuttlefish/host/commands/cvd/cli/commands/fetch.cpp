/*
 * Copyright (C) 2022 The Android Open Source Project
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

#include "host/commands/cvd/cli/commands/fetch.h"

#include <unistd.h>

#include <memory>
#include <string>
#include <vector>

#include <android-base/logging.h>
#include <android-base/strings.h>

#include "common/libs/utils/files.h"
#include "common/libs/utils/result.h"
#include "common/libs/utils/subprocess.h"
#include "common/libs/utils/tee_logging.h"
#include "host/commands/cvd/cache/cache.h"
#include "host/commands/cvd/cli/commands/command_handler.h"
#include "host/commands/cvd/cli/types.h"
#include "host/commands/cvd/fetch/fetch_cvd.h"
#include "host/commands/cvd/fetch/fetch_cvd_parser.h"
#include "host/commands/cvd/utils/common.h"

namespace cuttlefish {

namespace {

class CvdFetchCommandHandler : public CvdCommandHandler {
 public:
  Result<void> Handle(const CommandRequest& request) override;
  cvd_common::Args CmdList() const override { return {"fetch", "fetch_cvd"}; }
  Result<std::string> SummaryHelp() const override;
  bool ShouldInterceptHelp() const override { return true; }
  Result<std::string> DetailedHelp(std::vector<std::string>&) const override;
};

Result<void> CvdFetchCommandHandler::Handle(const CommandRequest& request) {
  CF_EXPECT(CanHandle(request));

  std::vector<std::string> args = request.SubcommandArguments();
  const FetchFlags flags = CF_EXPECT(FetchFlags::Parse(args));
  CF_EXPECT(EnsureDirectoryExists(flags.target_directory));

  std::string log_file = GetFetchLogsFileName(flags.target_directory);
  MetadataLevel metadata_level =
      isatty(0) ? MetadataLevel::ONLY_MESSAGE : MetadataLevel::FULL;
  ScopedTeeLogger logger(
      LogToStderrAndFiles({log_file}, "", metadata_level, flags.verbosity));

  Result<void> result = FetchCvdMain(flags);
  if (flags.build_api_flags.enable_caching) {
    const std::string cache_directory = PerUserCacheDir();
    LOG(INFO) << CF_EXPECTF(
        PruneCache(cache_directory, flags.build_api_flags.max_cache_size_gb),
        "Error pruning cache at {} to {}GB", cache_directory,
        flags.build_api_flags.max_cache_size_gb);
  }
  CF_EXPECT(std::move(result));
  return {};
}

Result<std::string> CvdFetchCommandHandler::SummaryHelp() const {
  return "Retrieve build artifacts based on branch and target names";
}

Result<std::string> CvdFetchCommandHandler::DetailedHelp(
    std::vector<std::string>&) const {
  std::vector<std::string> args = {"--help"};
  // TODO: b/389119573 - Should return the help text instead of printing it
  CF_EXPECT(FetchFlags::Parse(args));
  return {};
}

}  // namespace

std::unique_ptr<CvdCommandHandler> NewCvdFetchCommandHandler() {
  return std::unique_ptr<CvdCommandHandler>(new CvdFetchCommandHandler());
}

}  // namespace cuttlefish
