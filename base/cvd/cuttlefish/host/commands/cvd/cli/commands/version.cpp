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

#include "cuttlefish/host/commands/cvd/cli/commands/version.h"

#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include <fmt/format.h>
#include <json/value.h>

#include "cuttlefish/common/libs/utils/flag_parser.h"
#include "cuttlefish/common/libs/utils/proto.h"
#include "cuttlefish/common/libs/utils/result.h"
#include "cuttlefish/host/commands/cvd/cli/command_request.h"
#include "cuttlefish/host/commands/cvd/cli/commands/command_handler.h"
#include "cuttlefish/host/commands/cvd/cli/types.h"
#include "cuttlefish/host/commands/cvd/version/version.h"

namespace cuttlefish {
namespace {

constexpr char kSummaryHelpText[] =
    R"(Prints version of cvd client and cvd server)";

Result<bool> ProcessArguments(
    const std::vector<std::string>& subcommand_arguments) {
  std::vector<std::string> version_arguments = subcommand_arguments;
  bool json_formatted = false;
  std::vector<Flag> flags;
  flags.emplace_back(GflagsCompatFlag("json", json_formatted)
                         .Help("Output version information in JSON format."));

  CF_EXPECTF(ConsumeFlags(flags, version_arguments),
             "Failure processing arguments/flags: cvd version {}",
             fmt::join(subcommand_arguments, " "));
  return json_formatted;
}

class CvdVersionHandler : public CvdCommandHandler {
 public:
  CvdVersionHandler() = default;

  Result<void> Handle(const CommandRequest& request) override {
    CF_EXPECT(CanHandle(request));
    const bool json_formatted =
        CF_EXPECT(ProcessArguments(request.SubcommandArguments()));
    const VersionIdentifiers version_ids = GetVersionIds();
    if (json_formatted) {
      Json::Value json_output(Json::objectValue);
      json_output["package_version"] = version_ids.package;
      json_output["version_control_id"] = version_ids.version_control;
      std::cout << json_output.toStyledString();
    } else {
      std::cout << version_ids.ToPrettyString();
    }
    return {};
  }

  cvd_common::Args CmdList() const override { return {"version"}; }

  Result<std::string> SummaryHelp() const override { return kSummaryHelpText; }

  bool ShouldInterceptHelp() const override { return true; }

  Result<std::string> DetailedHelp(std::vector<std::string>&) const override {
    return kSummaryHelpText;
  }
};

}  // namespace

std::unique_ptr<CvdCommandHandler> NewCvdVersionHandler() {
  return std::unique_ptr<CvdCommandHandler>(new CvdVersionHandler());
}

}  // namespace cuttlefish
