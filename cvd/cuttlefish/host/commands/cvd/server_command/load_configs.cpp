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
#include <string>

#include <fruit/fruit.h>

#include "common/libs/fs/shared_buf.h"
#include "common/libs/utils/files.h"
#include "common/libs/utils/flag_parser.h"
#include "common/libs/utils/result.h"
#include "host/commands/cvd/command_sequence.h"
#include "host/commands/cvd/parser/load_configs_parser.h"
#include "host/commands/cvd/server.h"
#include "server_client.h"

namespace cuttlefish {

struct DemoCommandSequence {
  std::vector<InstanceLockFile> instance_locks;
  std::vector<RequestWithStdio> requests;
};

class LoadConfigsCommand : public CvdServerHandler {
 public:
  INJECT(LoadConfigsCommand(CommandSequenceExecutor& executor,
                            InstanceLockFileManager& lock_file_manager))
      : executor_(executor), lock_file_manager_(lock_file_manager) {}
  ~LoadConfigsCommand() = default;

  Result<bool> CanHandle(const RequestWithStdio& request) const override {
    auto invocation = ParseInvocation(request.Message());
    return invocation.command == "load";
  }

  Result<cvd::Response> Handle(const RequestWithStdio& request) override {
    std::unique_lock interrupt_lock(interrupt_mutex_);
    CF_EXPECT(!interrupted_, "Interrupted");
    CF_EXPECT(CF_EXPECT(CanHandle(request)));

    auto commands = CF_EXPECT(CreateCommandSequence(request));
    interrupt_lock.unlock();
    CF_EXPECT(executor_.Execute(commands.requests, request.Err()));

    for (auto& lock : commands.instance_locks) {
      CF_EXPECT(lock.Status(InUseState::kInUse));
    }

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
      static constexpr char kHelp[] = "Usage: cvd load";
      CF_EXPECT(WriteAll(request.Out(), kHelp, sizeof(kHelp)) == sizeof(kHelp));
      return {};
    }

    std::vector<std::string> serialized_data;
    Json::Value json_configs =
        CF_EXPECT(ParseJsonFile(config_path), "parsing input file failed");

    CF_EXPECT(ParseCvdConfigs(json_configs, serialized_data), "parsing json configs failed");

    DemoCommandSequence ret;

    auto lock = CF_EXPECT(lock_file_manager_.TryAcquireUnusedLock());
    CF_EXPECT(lock.has_value(), "Failed to acquire instance number (Load)");
    ret.instance_locks.emplace_back(std::move(*lock));

    std::vector<cvd::Request> req_protos;

    auto& launch_phone = *req_protos.emplace_back().mutable_command_request();
    launch_phone.set_working_directory(
        request.Message().command_request().working_directory());
    *launch_phone.mutable_env() = request.Message().command_request().env();

    /* cvd load will always crate instances in deamon mode (to be independent of
     terminal) and will enable reporting automatically(to run automatically
     without question during launch)
     */
    launch_phone.add_args("cvd");
    launch_phone.add_args("start");
    launch_phone.add_args("--daemon");
    launch_phone.add_args("--report_anonymous_usage_stats=y");
    for (auto& parsed_flag : serialized_data) {
      launch_phone.add_args(parsed_flag);
    }

    auto phone_instance = std::to_string(ret.instance_locks[0].Instance());
    launch_phone.add_args("--base_instance_num=" + phone_instance);

    /*Verbose is disabled by default*/
    auto dev_null = SharedFD::Open("/dev/null", O_RDWR);
    CF_EXPECT(dev_null->IsOpen(), dev_null->StrError());
    std::vector<SharedFD> fds = {dev_null, dev_null, dev_null};

    for (auto& request_proto : req_protos) {
      ret.requests.emplace_back(request.Client(), request_proto, fds,
                                request.Credentials());
    }

    return ret;
  }

 private:
  CommandSequenceExecutor& executor_;
  InstanceLockFileManager& lock_file_manager_;

  std::mutex interrupt_mutex_;
  bool interrupted_ = false;
};

fruit::Component<fruit::Required<CommandSequenceExecutor>>
LoadConfigsComponent() {
  return fruit::createComponent()
      .addMultibinding<CvdServerHandler, LoadConfigsCommand>();
}

}  // namespace cuttlefish
