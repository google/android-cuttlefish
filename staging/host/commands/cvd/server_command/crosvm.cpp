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

#include "host/commands/cvd/server_command/crosvm.h"

#include <android-base/strings.h>

#include <future>
#include <iostream>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "common/libs/fs/shared_buf.h"
#include "common/libs/utils/contains.h"
#include "common/libs/utils/subprocess.h"
#include "host/commands/cvd/flag.h"
#include "host/commands/cvd/selector/instance_group_record.h"
#include "host/commands/cvd/selector/instance_record.h"
#include "host/commands/cvd/selector/selector_constants.h"
#include "host/commands/cvd/server_command/server_handler.h"
#include "host/commands/cvd/server_command/subprocess_waiter.h"
#include "host/commands/cvd/server_command/utils.h"
#include "host/commands/cvd/types.h"
#include "host/libs/config/cuttlefish_config.h"

namespace cuttlefish {

class CvdCrosVmCommandHandler : public CvdServerHandler {
 public:
  INJECT(CvdCrosVmCommandHandler(InstanceManager& instance_manager))
      : instance_manager_{instance_manager},
        crosvm_operations_{"suspend", "resume", "snapshot"} {}

  Result<bool> CanHandle(const RequestWithStdio& request) const {
    auto invocation = ParseInvocation(request.Message());
    return Contains(crosvm_operations_, invocation.command);
  }

  Result<cvd::Response> Handle(const RequestWithStdio& request) override {
    std::unique_lock interrupt_lock(interruptible_);
    CF_EXPECT(!interrupted_, "Interrupted");
    CF_EXPECT(CanHandle(request));
    CF_EXPECT(VerifyPrecondition(request));
    const uid_t uid = request.Credentials()->uid;
    cvd_common::Envs envs =
        cvd_common::ConvertToEnvs(request.Message().command_request().env());

    auto [crosvm_op, subcmd_args] = ParseInvocation(request.Message());
    /*
     * crosvm suspend/resume/snapshot support --help only. Not --helpxml, etc.
     *
     * Otherwise, IsHelpSubcmd() should be used here instead.
     */
    auto help_flag = CvdFlag("help", false);
    cvd_common::Args subcmd_args_copy{subcmd_args};
    auto help_parse_result = help_flag.CalculateFlag(subcmd_args_copy);
    bool is_help = help_parse_result.ok() && (*help_parse_result);

    auto commands =
        is_help ? CF_EXPECT(HelpCommand(request, crosvm_op, subcmd_args, envs))
                : CF_EXPECT(NonHelpCommand(request, uid, crosvm_op, subcmd_args,
                                           envs));
    subprocess_waiters_ = std::vector<SubprocessWaiter>(commands.size());
    interrupt_lock.unlock();
    return CF_EXPECT(ConstructResponse(std::move(commands)));
  }

  Result<void> Interrupt() override {
    std::scoped_lock interrupt_lock(interruptible_);
    interrupted_ = true;
    for (auto& subprocess_waiter : subprocess_waiters_) {
      CF_EXPECT(subprocess_waiter.Interrupt());
    }
    return {};
  }
  cvd_common::Args CmdList() const override {
    return cvd_common::Args(crosvm_operations_.begin(),
                            crosvm_operations_.end());
  }

 private:
  Result<cvd::Response> ConstructResponse(std::vector<Command> commands) {
    std::vector<std::future<Result<siginfo_t>>> infop_futures;
    infop_futures.reserve(commands.size());
    auto worker = [this](const int idx, Command command) -> Result<siginfo_t> {
      std::unique_lock interrupt_lock(interruptible_);
      CF_EXPECT(!interrupted_, "Interrupted");
      SubprocessOptions options;
      auto& subprocess_waiter = subprocess_waiters_[idx];
      CF_EXPECT(subprocess_waiter.Setup(command.Start(options)));
      interrupt_lock.unlock();
      return CF_EXPECT(subprocess_waiter.Wait());
    };
    size_t idx = 0;
    for (auto& command : commands) {
      std::future<Result<siginfo_t>> infop_future =
          std::async(std::launch::async, worker, idx, std::move(command));
      infop_futures.push_back(std::move(infop_future));
      idx++;
    }
    commands.clear();

    bool ok = true;
    std::stringstream error_msg;
    for (auto& infop_future : infop_futures) {
      auto infop = std::move(infop_future.get());
      if (!infop.ok()) {
        LOG(ERROR) << infop.error().Trace();
        ok = false;
        continue;
      }
      if (infop->si_code == CLD_EXITED && infop->si_status == 0) {
        continue;
      }
      // error
      ok = false;
      std::string status_code_str = std::to_string(infop->si_status);
      if (infop->si_code == CLD_EXITED) {
        error_msg << "Exited with code " << status_code_str << std::endl;
      } else if (infop->si_code == CLD_KILLED) {
        error_msg << "Exited with signal " << status_code_str << std::endl;
      } else {
        error_msg << "Quit with code " << status_code_str << std::endl;
      }
    }

    cvd::Response response;
    response.mutable_command_response();  // set oneof field
    auto& status = *response.mutable_status();
    if (ok) {
      status.set_code(cvd::Status::OK);
    }
    {
      status.set_code(cvd::Status::INTERNAL);
      status.set_message(error_msg.str());
    }
    return response;
  }

  Result<std::vector<Command>> HelpCommand(const RequestWithStdio& request,
                                           const std::string& crosvm_op,
                                           const cvd_common::Args& subcmd_args,
                                           cvd_common::Envs envs) {
    CF_EXPECT(Contains(envs, kAndroidHostOut));
    cvd_common::Args crosvm_args{crosvm_op};
    crosvm_args.insert(crosvm_args.end(), subcmd_args.begin(),
                       subcmd_args.end());
    Command help_command = CF_EXPECT(
        ConstructCvdHelpCommand("crosvm", envs, crosvm_args, request));
    std::vector<Command> help_command_in_vector;
    help_command_in_vector.push_back(std::move(help_command));
    return help_command_in_vector;
  }

  Result<std::vector<Command>> NonHelpCommand(
      const RequestWithStdio& request, const uid_t uid,
      const std::string& crosvm_op, const cvd_common::Args& subcmd_args,
      const cvd_common::Envs& envs) {
    const auto& selector_opts =
        request.Message().command_request().selector_opts();
    const auto selector_args = cvd_common::ConvertToArgs(selector_opts.args());

    if (CF_EXPECT(HasInstanceSpecificOption(selector_args, envs))) {
      auto instance =
          CF_EXPECT(instance_manager_.SelectInstance(selector_args, envs, uid));
      return CF_EXPECT(NonHelpInstanceCommand(request, instance, crosvm_op,
                                              subcmd_args, envs));
    }
    auto instance_group =
        CF_EXPECT(instance_manager_.SelectGroup(selector_args, envs, uid));
    return CF_EXPECT(NonHelpGroupCommand(request, instance_group, crosvm_op,
                                         subcmd_args, envs));
  }

  Result<std::vector<Command>> NonHelpGroupCommand(
      const RequestWithStdio& request,
      const InstanceManager::LocalInstanceGroup& instance_group,
      const std::string& crosvm_op, const cvd_common::Args& subcmd_args,
      const cvd_common::Envs& envs) {
    std::vector<Command> commands;
    auto& instances = instance_group.Instances();
    for (const auto& instance : instances) {
      auto instance_commands = CF_EXPECT(NonHelpInstanceCommand(
          request, instance->GetCopy(), crosvm_op, subcmd_args, envs));
      CF_EXPECT_EQ(instance_commands.size(), 1);
      commands.push_back(std::move(instance_commands.front()));
    }
    return commands;
  }

  Result<std::vector<Command>> NonHelpInstanceCommand(
      const RequestWithStdio& request,
      const InstanceManager::LocalInstance::Copy& instance,
      const std::string& crosvm_op, const cvd_common::Args& subcmd_args,
      const cvd_common::Envs& envs) {
    const auto& instance_group = instance.ParentGroup();
    const auto instance_id = instance.InstanceId();
    auto home = instance_group.HomeDir();
    const auto socket_file_path =
        ConcatToString(home, "/cuttlefish_runtime.", instance_id,
                       "/internal/"
                       "crosvm_control.sock");

    auto android_host_out = instance_group.HostArtifactsPath();
    auto crosvm_bin_path = ConcatToString(android_host_out, "/bin/crosvm");

    cvd_common::Args crosvm_args{crosvm_op};
    crosvm_args.insert(crosvm_args.end(), subcmd_args.begin(),
                       subcmd_args.end());
    crosvm_args.push_back(socket_file_path);
    Command non_help_command = CF_EXPECT(
        ConstructCvdGenericNonHelpCommand({.bin_file = "crosvm",
                                           .envs = envs,
                                           .cmd_args = std::move(crosvm_args),
                                           .android_host_out = android_host_out,
                                           .home = home,
                                           .verbose = true},
                                          request));
    std::vector<Command> non_help_command_in_vector;
    non_help_command_in_vector.push_back(std::move(non_help_command));
    return non_help_command_in_vector;
  }

  Result<bool> HasInstanceSpecificOption(cvd_common::Args selector_args,
                                         const cvd_common::Envs& envs) const {
    auto instance_name_flag = CF_EXPECT(selector::SelectorFlags::Get().GetFlag(
        selector::SelectorFlags::kInstanceName));
    std::optional<std::string> instance_name_opt =
        CF_EXPECT(instance_name_flag.FilterFlag<std::string>(selector_args));
    if (instance_name_opt) {
      return true;
    }
    return Contains(envs, kCuttlefishInstanceEnvVarName);
  }

  InstanceManager& instance_manager_;
  std::vector<SubprocessWaiter> subprocess_waiters_;
  std::mutex interruptible_;
  bool interrupted_ = false;
  std::vector<std::string> crosvm_operations_;
};

fruit::Component<fruit::Required<InstanceManager>> CvdCrosVmComponent() {
  return fruit::createComponent()
      .addMultibinding<CvdServerHandler, CvdCrosVmCommandHandler>();
}

}  // namespace cuttlefish
