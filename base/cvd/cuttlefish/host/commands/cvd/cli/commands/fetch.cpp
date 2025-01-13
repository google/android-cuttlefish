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

#include <memory>
#include <string>
#include <vector>

#include <android-base/strings.h>

#include "common/libs/utils/result.h"
#include "common/libs/utils/subprocess.h"
#include "host/commands/cvd/cli/commands/command_handler.h"
#include "host/commands/cvd/cli/types.h"
#include "host/commands/cvd/fetch/fetch_cvd.h"

namespace cuttlefish {

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
  CF_EXPECT(FetchCvdMain(args));
  return {};
}

Result<std::string> CvdFetchCommandHandler::SummaryHelp() const {
  return "Retrieve build artifacts based on branch and target names";
}

Result<std::string> CvdFetchCommandHandler::DetailedHelp(
    std::vector<std::string>&) const {
  std::vector<std::string> args = {"--help"};
  // TODO: b/389119573 - Should return the help text instead of printing it
  CF_EXPECT(FetchCvdMain(args));
  return {};
}

std::unique_ptr<CvdCommandHandler> NewCvdFetchCommandHandler() {
  return std::unique_ptr<CvdCommandHandler>(new CvdFetchCommandHandler());
}

}  // namespace cuttlefish
