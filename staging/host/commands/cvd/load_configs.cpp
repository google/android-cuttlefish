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
#include "host/commands/cvd/load_configs.h"

#include <chrono>
#include <mutex>
#include <sstream>
#include <string>

#include <fruit/fruit.h>

#include "common/libs/fs/shared_buf.h"
#include "common/libs/utils/files.h"
#include "common/libs/utils/flag_parser.h"
#include "common/libs/utils/result.h"
#include "host/commands/cvd/command_sequence.h"
#include "host/commands/cvd/parser/load_configs_parser.h"
#include "host/commands/cvd/selector/selector_constants.h"
#include "host/commands/cvd/server.h"
#include "host/commands/cvd/server_client.h"
#include "host/commands/cvd/server_command/utils.h"
#include "host/commands/cvd/types.h"

namespace cuttlefish {

namespace {

struct DemoCommandSequence {
  std::vector<RequestWithStdio> requests;
};

}  // namespace

class LoadConfigsCommand : public CvdServerHandler {
 public:
  INJECT(LoadConfigsCommand(CommandSequenceExecutor& executor))
      : executor_(executor) {}
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
    CF_EXPECT(executor_.Execute(commands.requests, request.Err()));

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

  Result<DemoCommandSequence> CreateCommandSequence(
      const RequestWithStdio& request) {
    bool help = false;

    std::vector<Flag> flags;
    flags.emplace_back(GflagsCompatFlag("help", help));
    std::string config_path;
    flags.emplace_back(GflagsCompatFlag("config_path", config_path));

    auto args = ParseInvocation(request.Message()).arguments;
    CF_EXPECT(ParseFlags(flags, args));

    if (help) {
      std::stringstream help_msg_stream;
      help_msg_stream << "Usage: cvd " << kLoadSubCmd << std::endl;
      const auto help_msg = help_msg_stream.str();
      CF_EXPECT(WriteAll(request.Out(), help_msg) == help_msg.size());
      return {};
    }

    Json::Value json_configs =
        CF_EXPECT(ParseJsonFile(config_path), "parsing input file failed");

    auto cvd_flags =
        CF_EXPECT(ParseCvdConfigs(json_configs), "parsing json configs failed");

    DemoCommandSequence ret;

    std::vector<cvd::Request> req_protos;

    auto& launch_phone = *req_protos.emplace_back().mutable_command_request();
    launch_phone.set_working_directory(
        request.Message().command_request().working_directory());
    *launch_phone.mutable_env() = request.Message().command_request().env();

    /* cvd load will always create instances in deamon mode (to be independent
     of terminal) and will enable reporting automatically (to run automatically
     without question during launch)
     */
    launch_phone.add_args("cvd");
    launch_phone.add_args("start");
    launch_phone.add_args("--daemon");
    for (auto& parsed_flag : cvd_flags.launch_cvd_flags) {
      launch_phone.add_args(parsed_flag);
    }

    launch_phone.mutable_selector_opts()->add_args(
        std::string("--") + selector::kDisableDefaultGroupOpt);

    /*Verbose is disabled by default*/
    auto dev_null = SharedFD::Open("/dev/null", O_RDWR);
    CF_EXPECT(dev_null->IsOpen(), dev_null->StrError());
    std::vector<SharedFD> fds = {dev_null, dev_null, dev_null};

    for (auto& request_proto : req_protos) {
      ret.requests.emplace_back(RequestWithStdio(
          request.Client(), request_proto, fds, request.Credentials()));
    }

    return ret;
  }

 private:
  static constexpr char kLoadSubCmd[] = "load";

  CommandSequenceExecutor& executor_;

  std::mutex interrupt_mutex_;
  bool interrupted_ = false;
};

fruit::Component<fruit::Required<CommandSequenceExecutor>>
LoadConfigsComponent() {
  return fruit::createComponent()
      .addMultibinding<CvdServerHandler, LoadConfigsCommand>();
}

}  // namespace cuttlefish
