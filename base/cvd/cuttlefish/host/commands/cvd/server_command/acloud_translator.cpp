/*
 * Copyright (C) 2023 The Android Open Source Project
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

#include "host/commands/cvd/server_command/acloud_translator.h"

#include "common/libs/utils/flag_parser.h"
#include "common/libs/utils/result.h"
#include "cuttlefish/host/commands/cvd/cvd_server.pb.h"
#include "host/commands/cvd/command_request.h"
#include "host/commands/cvd/server_command/server_handler.h"
#include "host/commands/cvd/server_command/utils.h"
#include "host/commands/cvd/types.h"

namespace cuttlefish {

static constexpr char kTranslatorHelpMessage[] =
    R"(Cuttlefish Virtual Device (CVD) CLI.

usage: cvd acloud translator <args>

Args:
  --opt-out              Opt-out CVD Acloud and choose to run original Python Acloud.
  --opt-in               Opt-in and run CVD Acloud as default.
Both -opt-out and --opt-in are mutually exclusive.
)";

class AcloudTranslatorCommand : public CvdServerHandler {
 public:
  AcloudTranslatorCommand(InstanceManager& instance_manager) : instance_manager_(instance_manager) {}
  ~AcloudTranslatorCommand() = default;

  Result<bool> CanHandle(const CommandRequest& request) const override {
    auto invocation = ParseInvocation(request);
    if (invocation.arguments.size() >= 2) {
      if (invocation.command == "acloud" &&
          invocation.arguments[0] == "translator") {
        return true;
      }
    }
    return false;
  }

  // not intended to be used by the user
  cvd_common::Args CmdList() const override { return {}; }
  // not intended to show up in help
  Result<std::string> SummaryHelp() const override { return ""; }
  bool ShouldInterceptHelp() const override { return false; }
  Result<std::string> DetailedHelp(std::vector<std::string>&) const override {
    return "";
  }

  Result<cvd::Response> Handle(const CommandRequest& request) override {
    CF_EXPECT(CanHandle(request));
    auto invocation = ParseInvocation(request);
    if (invocation.arguments.empty() || invocation.arguments.size() < 2) {
      return CF_ERR("Translator command not support");
    }

    // cvd acloud translator --opt-out
    // cvd acloud translator --opt-in
    cvd::Response response;
    response.mutable_command_response();
    bool help = false;
    bool flag_optout = false;
    bool flag_optin = false;
    std::vector<Flag> translator_flags = {
        GflagsCompatFlag("help", help),
        GflagsCompatFlag("opt-out", flag_optout),
        GflagsCompatFlag("opt-in", flag_optin),
    };
    CF_EXPECT(ConsumeFlags(translator_flags, invocation.arguments),
              "Failed to process translator flag.");
    if (help) {
      std::cout << kTranslatorHelpMessage;
      return response;
    }
    CF_EXPECT(flag_optout != flag_optin,
              "Only one of --opt-out or --opt-in should be given.");
    CF_EXPECT(instance_manager_.SetAcloudTranslatorOptout(flag_optout));
    return response;
  }

 private:
  InstanceManager& instance_manager_;
};

std::unique_ptr<CvdServerHandler> NewAcloudTranslatorCommand(
InstanceManager& instance_manager) {
  return std::unique_ptr<CvdServerHandler>(new AcloudTranslatorCommand(instance_manager));
}

}  // namespace cuttlefish
