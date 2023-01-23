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

#include "host/commands/cvd/server_command/generic.h"

#include <sys/types.h>

#include <mutex>

#include <android-base/file.h>

#include "common/libs/fs/shared_fd.h"
#include "common/libs/utils/contains.h"
#include "common/libs/utils/environment.h"
#include "common/libs/utils/files.h"
#include "common/libs/utils/result.h"
#include "common/libs/utils/subprocess.h"
#include "cvd_server.pb.h"
#include "host/commands/cvd/command_sequence.h"
#include "host/commands/cvd/instance_manager.h"
#include "host/commands/cvd/server_command/server_handler.h"
#include "host/commands/cvd/server_command/subprocess_waiter.h"
#include "host/commands/cvd/server_command/utils.h"
#include "host/commands/cvd/types.h"
#include "host/libs/config/cuttlefish_config.h"
#include "host/libs/config/instance_nums.h"

namespace cuttlefish {

class CvdGenericCommandHandler : public CvdServerHandler {
 public:
  INJECT(CvdGenericCommandHandler(InstanceManager& instance_manager,
                                  SubprocessWaiter& subprocess_waiter))
      : instance_manager_(instance_manager),
        subprocess_waiter_(subprocess_waiter) {}

  Result<bool> CanHandle(const RequestWithStdio& request) const;
  Result<cvd::Response> Handle(const RequestWithStdio& request) override;
  Result<void> Interrupt() override;
  cvd_common::Args CmdList() const override;

 private:
  struct CommandInvocationInfo {
    std::string command;
    std::string bin;
    std::string home;
    std::string host_artifacts_path;
    uid_t uid;
    std::vector<std::string> args;
    cvd_common::Envs envs;
  };
  std::optional<CommandInvocationInfo> ExtractInfo(
      const RequestWithStdio& request) const;

  InstanceManager& instance_manager_;
  SubprocessWaiter& subprocess_waiter_;
  std::mutex interruptible_;
  bool interrupted_ = false;

  static constexpr char kHostBugreportBin[] = "cvd_internal_host_bugreport";
  static constexpr char kDisplayBin[] = "cvd_internal_display";
  static constexpr char kEnvBin[] = "cvd_internal_env";
  static constexpr char kLnBin[] = "ln";
  static constexpr char kMkdirBin[] = "mkdir";

  static constexpr char kClearBin[] =
      "clear_placeholder";  // Unused, runs CvdClear()

  static const std::map<std::string, std::string> command_to_binary_map_;
};

Result<bool> CvdGenericCommandHandler::CanHandle(
    const RequestWithStdio& request) const {
  auto invocation = ParseInvocation(request.Message());
  return Contains(command_to_binary_map_, invocation.command);
}

Result<void> CvdGenericCommandHandler::Interrupt() {
  std::scoped_lock interrupt_lock(interruptible_);
  interrupted_ = true;
  CF_EXPECT(subprocess_waiter_.Interrupt());
  return {};
}

Result<cvd::Response> CvdGenericCommandHandler::Handle(
    const RequestWithStdio& request) {
  std::unique_lock interrupt_lock(interruptible_);
  if (interrupted_) {
    return CF_ERR("Interrupted");
  }
  CF_EXPECT(CanHandle(request));
  CF_EXPECT(request.Credentials() != std::nullopt);
  const uid_t uid = request.Credentials()->uid;

  cvd::Response response;
  response.mutable_command_response();

  auto [meets_precondition, error_message] = VerifyPrecondition(request);
  if (!meets_precondition) {
    response.mutable_status()->set_code(cvd::Status::FAILED_PRECONDITION);
    response.mutable_status()->set_message(error_message);
    return response;
  }

  auto invocation_info_opt = ExtractInfo(request);
  CF_EXPECT(invocation_info_opt != std::nullopt);
  auto invocation_info = std::move(*invocation_info_opt);

  if (invocation_info.bin == kClearBin) {
    *response.mutable_status() =
        instance_manager_.CvdClear(request.Out(), request.Err());
    return response;
  }

  std::string bin_path = invocation_info.bin;
  if (invocation_info.bin != kMkdirBin && invocation_info.bin != kLnBin) {
    auto assembly_info_result =
        instance_manager_.GetInstanceGroupInfo(uid, invocation_info.home);
    if (assembly_info_result.ok()) {
      auto assembly_info = assembly_info_result.value();
      bin_path =
          assembly_info.host_artifacts_path + "/bin/" + invocation_info.bin;
    } else {
      bin_path =
          invocation_info.host_artifacts_path + "/bin/" + invocation_info.bin;
    }
  }

  ConstructCommandParam construct_cmd_param{
      .bin_path = bin_path,
      .home = invocation_info.home,
      .args = invocation_info.args,
      .envs = invocation_info.envs,
      .working_dir = request.Message().command_request().working_directory(),
      .command_name = invocation_info.bin,
      .in = request.In(),
      .out = request.Out(),
      .err = request.Err()};
  Command command = CF_EXPECT(ConstructCommand(construct_cmd_param));

  SubprocessOptions options;
  if (request.Message().command_request().wait_behavior() ==
      cvd::WAIT_BEHAVIOR_START) {
    options.ExitWithParent(false);
  }
  CF_EXPECT(subprocess_waiter_.Setup(command.Start(options)));

  if (request.Message().command_request().wait_behavior() ==
      cvd::WAIT_BEHAVIOR_START) {
    response.mutable_status()->set_code(cvd::Status::OK);
    return response;
  }

  interrupt_lock.unlock();

  auto infop = CF_EXPECT(subprocess_waiter_.Wait());

  if (infop.si_code == CLD_EXITED && invocation_info.bin == kStopBin) {
    instance_manager_.RemoveInstanceGroup(uid, invocation_info.home);
  }

  return ResponseFromSiginfo(infop);
}

std::vector<std::string> CvdGenericCommandHandler::CmdList() const {
  std::vector<std::string> subcmd_list;
  subcmd_list.reserve(command_to_binary_map_.size());
  for (const auto& [cmd, _] : command_to_binary_map_) {
    subcmd_list.emplace_back(cmd);
  }
  return subcmd_list;
}

std::optional<CvdGenericCommandHandler::CommandInvocationInfo>
CvdGenericCommandHandler::ExtractInfo(const RequestWithStdio& request) const {
  auto result_opt = request.Credentials();
  if (!result_opt) {
    return std::nullopt;
  }
  const uid_t uid = result_opt->uid;

  auto [command, args] = ParseInvocation(request.Message());
  if (!Contains(command_to_binary_map_, command)) {
    return std::nullopt;
  }
  const auto& bin = command_to_binary_map_.at(command);
  cvd_common::Envs envs =
      cvd_common::ConvertToEnvs(request.Message().command_request().env());
  std::string home =
      Contains(envs, "HOME") ? envs.at("HOME") : StringFromEnv("HOME", ".");
  if (!Contains(envs, "ANDROID_HOST_OUT") ||
      !DirectoryExists(envs.at("ANDROID_HOST_OUT"))) {
    return std::nullopt;
  }
  const auto host_artifacts_path = envs.at("ANDROID_HOST_OUT");
  // TODO(kwstephenkim): eat --base_instance_num and --num_instances
  // or --instance_nums, and override/delete kCuttlefishInstanceEnvVarName
  // in envs
  CommandInvocationInfo result = {.command = command,
                                  .bin = bin,
                                  .home = home,
                                  .host_artifacts_path = host_artifacts_path,
                                  .uid = uid,
                                  .args = args,
                                  .envs = envs};
  result.envs["HOME"] = home;
  return {result};
}

const std::map<std::string, std::string>
    CvdGenericCommandHandler::command_to_binary_map_ = {
        {"host_bugreport", kHostBugreportBin},
        {"cvd_host_bugreport", kHostBugreportBin},
        {"status", kStatusBin},
        {"cvd_status", kStatusBin},
        {"stop", kStopBin},
        {"stop_cvd", kStopBin},
        {"clear", kClearBin},
        {"mkdir", kMkdirBin},
        {"ln", kLnBin},
        {"display", kDisplayBin},
        {"env", kEnvBin},
};

fruit::Component<fruit::Required<InstanceManager, SubprocessWaiter>>
cvdGenericCommandComponent() {
  return fruit::createComponent()
      .addMultibinding<CvdServerHandler, CvdGenericCommandHandler>();
}

}  // namespace cuttlefish
