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
#include "host/commands/cvd/server_command/load_configs.h"

#include <iostream>
#include <mutex>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include <json/json.h>

#include "common/libs/fs/shared_buf.h"
#include "common/libs/utils/result.h"
#include "host/commands/cvd/command_sequence.h"
#include "host/commands/cvd/common_utils.h"
#include "host/commands/cvd/parser/load_configs_parser.h"
#include "host/commands/cvd/selector/selector_constants.h"
#include "host/commands/cvd/server_client.h"
#include "host/commands/cvd/server_command/utils.h"
#include "host/commands/cvd/types.h"

namespace cuttlefish {
namespace {

constexpr char kSummaryHelpText[] =
    R"(Loads the given JSON configuration file and launches devices based on the options provided)";

constexpr char kDetailedHelpText[] = R"(
Warning: This command is deprecated, use cvd start --config_file instead.

Usage:
cvd load <config_filepath> [--override=<key>:<value>]

Reads the fields in the JSON configuration file and translates them to corresponding start command and flags.  

Optionally fetches remote artifacts prior to launching the cuttlefish environment.

The --override flag can be used to give new values for properties in the config file without needing to edit the file directly.  Convenient for one-off invocations.
)";

}  // namespace

class LoadConfigsCommand : public CvdServerHandler {
 public:
  LoadConfigsCommand(CommandSequenceExecutor& executor) : executor_(executor) {}
  ~LoadConfigsCommand() = default;

  Result<bool> CanHandle(const RequestWithStdio& request) const override {
    auto invocation = ParseInvocation(request.Message());
    return invocation.command == kLoadSubCmd;
  }

  Result<cvd::Response> Handle(const RequestWithStdio& request) override {
    std::unique_lock interrupt_lock(interrupt_mutex_);
    CF_EXPECT(!interrupted_, "Interrupted");
    CF_EXPECT(CF_EXPECT(CanHandle(request)));

    auto commands = CF_EXPECT(CreateCommandSequence(request));
    interrupt_lock.unlock();
    CF_EXPECT(executor_.Execute(commands, request.Err()));

    cvd::Response response;
    response.mutable_command_response();
    return response;
  }

  Result<void> Interrupt() override {
    std::scoped_lock interrupt_lock(interrupt_mutex_);
    interrupted_ = true;
    CF_EXPECT(executor_.Interrupt());
    return {};
  }

  cvd_common::Args CmdList() const override { return {kLoadSubCmd}; }

  Result<std::string> SummaryHelp() const override { return kSummaryHelpText; }

  bool ShouldInterceptHelp() const override { return true; }

  Result<std::string> DetailedHelp(std::vector<std::string>&) const override {
    return kDetailedHelpText;
  }

  Result<std::vector<RequestWithStdio>> CreateCommandSequence(
      const RequestWithStdio& request) {
    auto args = ParseInvocation(request.Message()).arguments;
    auto working_directory =
        request.Message().command_request().working_directory();
    const LoadFlags flags = CF_EXPECT(GetFlags(args, working_directory));
    auto cvd_flags = CF_EXPECT(GetCvdFlags(flags));

    std::vector<cvd::Request> req_protos;
    const auto& client_env = request.Message().command_request().env();

    if (!cvd_flags.fetch_cvd_flags.empty()) {
      auto& fetch_cmd = *req_protos.emplace_back().mutable_command_request();
      *fetch_cmd.mutable_env() = client_env;
      fetch_cmd.add_args("cvd");
      fetch_cmd.add_args("fetch");
      for (const auto& flag : cvd_flags.fetch_cvd_flags) {
        fetch_cmd.add_args(flag);
      }
    }

    auto& mkdir_cmd = *req_protos.emplace_back().mutable_command_request();
    *mkdir_cmd.mutable_env() = client_env;
    mkdir_cmd.add_args("cvd");
    mkdir_cmd.add_args("mkdir");
    mkdir_cmd.add_args("-p");
    mkdir_cmd.add_args(cvd_flags.load_directories.launch_home_directory);

    auto& launch_cmd = *req_protos.emplace_back().mutable_command_request();
    launch_cmd.set_working_directory(
        cvd_flags.load_directories.host_package_directory);
    *launch_cmd.mutable_env() = client_env;
    (*launch_cmd.mutable_env())["HOME"] =
        cvd_flags.load_directories.launch_home_directory;
    (*launch_cmd.mutable_env())[kAndroidHostOut] =
        cvd_flags.load_directories.host_package_directory;
    (*launch_cmd.mutable_env())[kAndroidSoongHostOut] =
        cvd_flags.load_directories.host_package_directory;
    if (Contains(*launch_cmd.mutable_env(), kAndroidProductOut)) {
      (*launch_cmd.mutable_env()).erase(kAndroidProductOut);
    }

    /* cvd load will always create instances in daemon mode (to be independent
     of terminal) and will enable reporting automatically (to run automatically
     without question during launch)
     */
    launch_cmd.add_args("cvd");
    launch_cmd.add_args("start");
    launch_cmd.add_args("--daemon");
    for (const auto& parsed_flag : cvd_flags.launch_cvd_flags) {
      launch_cmd.add_args(parsed_flag);
    }
    // Add system flag for multi-build scenario
    launch_cmd.add_args(cvd_flags.load_directories.system_image_directory_flag);

    auto selector_opts = launch_cmd.mutable_selector_opts();

    for (const auto& flag : cvd_flags.selector_flags) {
      selector_opts->add_args(flag);
    }

    /*Verbose is disabled by default*/
    auto dev_null = SharedFD::Open("/dev/null", O_RDWR);
    CF_EXPECT(dev_null->IsOpen(), dev_null->StrError());
    std::vector<SharedFD> fds = {dev_null, dev_null, dev_null};
    std::vector<RequestWithStdio> ret;

    for (auto& request_proto : req_protos) {
      ret.emplace_back(RequestWithStdio(request.Client(), request_proto, fds,
                                        request.Credentials()));
    }

    return ret;
  }

 private:
  static constexpr char kLoadSubCmd[] = "load";

  CommandSequenceExecutor& executor_;

  std::mutex interrupt_mutex_;
  bool interrupted_ = false;
};

std::unique_ptr<CvdServerHandler> NewLoadConfigsCommand(
    CommandSequenceExecutor& executor) {
  return std::unique_ptr<CvdServerHandler>(new LoadConfigsCommand(executor));
}

}  // namespace cuttlefish
