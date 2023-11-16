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

#include <functional>
#include <mutex>

#include <android-base/file.h>
#include <android-base/scopeguard.h>

#include "common/libs/fs/shared_fd.h"
#include "common/libs/utils/contains.h"
#include "common/libs/utils/environment.h"
#include "common/libs/utils/files.h"
#include "common/libs/utils/result.h"
#include "common/libs/utils/subprocess.h"
#include "cvd_server.pb.h"
#include "host/commands/cvd/command_sequence.h"
#include "host/commands/cvd/common_utils.h"
#include "host/commands/cvd/instance_manager.h"
#include "host/commands/cvd/selector/selector_constants.h"
#include "host/commands/cvd/server_command/host_tool_target_manager.h"
#include "host/commands/cvd/server_command/server_handler.h"
#include "host/commands/cvd/server_command/subprocess_waiter.h"
#include "host/commands/cvd/server_command/utils.h"
#include "host/commands/cvd/types.h"
#include "host/libs/config/cuttlefish_config.h"
#include "host/libs/config/instance_nums.h"

namespace cuttlefish {

class CvdGenericCommandHandler : public CvdServerHandler {
 public:
  CvdGenericCommandHandler(InstanceManager& instance_manager,
                           SubprocessWaiter& subprocess_waiter,
                           HostToolTargetManager& host_tool_target_manager);

  Result<bool> CanHandle(const RequestWithStdio& request) const;
  Result<cvd::Response> Handle(const RequestWithStdio& request) override;
  Result<void> Interrupt() override;
  cvd_common::Args CmdList() const override;

 private:
  struct CommandInvocationInfo {
    std::string command;
    std::string bin;
    std::string bin_path;
    std::string home;
    std::string host_artifacts_path;
    uid_t uid;
    std::vector<std::string> args;
    cvd_common::Envs envs;
  };
  struct ExtractedInfo {
    CommandInvocationInfo invocation_info;
    std::optional<selector::LocalInstanceGroup> group;
  };
  Result<ExtractedInfo> ExtractInfo(const RequestWithStdio& request) const;
  Result<std::string> GetBin(const std::string& subcmd) const;
  Result<std::string> GetBin(const std::string& subcmd,
                             const std::string& host_artifacts_path) const;
  bool IsStopCommand(const std::string& subcmd) const {
    return subcmd == "stop" || subcmd == "stop_cvd";
  }
  // whether the "bin" is cvd bins like stop_cvd or not (e.g. ln, ls, mkdir)
  // The information to fire the command might be different. This information
  // is about what the executable binary is and how to find it.
  struct BinPathInfo {
    std::string bin_;
    std::string bin_path_;
    std::string host_artifacts_path_;
  };
  Result<BinPathInfo> NonCvdBinPath(const std::string& subcmd,
                                    const cvd_common::Envs& envs) const;
  Result<BinPathInfo> CvdHelpBinPath(const std::string& subcmd,
                                     const cvd_common::Envs& envs) const;

  InstanceManager& instance_manager_;
  SubprocessWaiter& subprocess_waiter_;
  HostToolTargetManager& host_tool_target_manager_;
  std::mutex interruptible_;
  bool interrupted_ = false;
  using BinGeneratorType = std::function<Result<std::string>(
      const std::string& host_artifacts_path)>;
  std::map<std::string, std::string> command_to_binary_map_;

  static constexpr char kHostBugreportBin[] = "cvd_internal_host_bugreport";
  static constexpr char kLnBin[] = "ln";
  static constexpr char kMkdirBin[] = "mkdir";
  static constexpr char kClearBin[] =
      "clear_placeholder";  // Unused, runs CvdClear()
  // Only indicates that host_tool_target_manager_ should generate at runtime
  static constexpr char kBinGeneratedAtRuntime[] =
      "host_tool_manager_generates_at_runtime_placeholder";
};

CvdGenericCommandHandler::CvdGenericCommandHandler(
    InstanceManager& instance_manager, SubprocessWaiter& subprocess_waiter,
    HostToolTargetManager& host_tool_target_manager)
    : instance_manager_(instance_manager),
      subprocess_waiter_(subprocess_waiter),
      host_tool_target_manager_(host_tool_target_manager),
      command_to_binary_map_{{"host_bugreport", kHostBugreportBin},
                             {"cvd_host_bugreport", kHostBugreportBin},
                             {"stop", kBinGeneratedAtRuntime},
                             {"stop_cvd", kBinGeneratedAtRuntime},
                             {"clear", kClearBin},
                             {"mkdir", kMkdirBin},
                             {"ln", kLnBin}} {}

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

  auto precondition_verified = VerifyPrecondition(request);
  if (!precondition_verified.ok()) {
    response.mutable_status()->set_code(cvd::Status::FAILED_PRECONDITION);
    response.mutable_status()->set_message(
        precondition_verified.error().Message());
    return response;
  }
  auto [invocation_info, group_opt] = CF_EXPECT(ExtractInfo(request));
  if (invocation_info.bin == kClearBin) {
    *response.mutable_status() =
        instance_manager_.CvdClear(request.Out(), request.Err());
    return response;
  }

  ConstructCommandParam construct_cmd_param{
      .bin_path = invocation_info.bin_path,
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

  bool is_stop = IsStopCommand(invocation_info.command);

  // captured structured bindings are a C++20 extension
  // so we need [group_ptr] instead of [&group_opt]
  auto* group_ptr = (group_opt ? std::addressof(*group_opt) : nullptr);
  android::base::ScopeGuard exit_action([this, is_stop, group_ptr]() {
    if (!is_stop) {
      return;
    }
    if (!group_ptr) {
      return;
    }
    for (const auto& instance : group_ptr->Instances()) {
      auto lock = instance_manager_.TryAcquireLock(instance->InstanceId());
      if (lock.ok() && (*lock)) {
        (*lock)->Status(InUseState::kNotInUse);
        continue;
      }
      LOG(ERROR) << "InstanceLockFileManager failed to acquire lock for #"
                 << instance->InstanceId();
    }
  });

  if (request.Message().command_request().wait_behavior() ==
      cvd::WAIT_BEHAVIOR_START) {
    response.mutable_status()->set_code(cvd::Status::OK);
    return response;
  }

  interrupt_lock.unlock();

  auto infop = CF_EXPECT(subprocess_waiter_.Wait());

  if (infop.si_code == CLD_EXITED && IsStopCommand(invocation_info.command)) {
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

Result<CvdGenericCommandHandler::BinPathInfo>
CvdGenericCommandHandler::NonCvdBinPath(const std::string& subcmd,
                                        const cvd_common::Envs& envs) const {
  auto bin_path_base = CF_EXPECT(GetBin(subcmd));
  // no need of executable directory. Will look up by PATH
  // bin_path_base is like ln, mkdir, etc.
  return BinPathInfo{.bin_ = bin_path_base,
                     .bin_path_ = bin_path_base,
                     .host_artifacts_path_ = envs.at(kAndroidHostOut)};
}

Result<CvdGenericCommandHandler::BinPathInfo>
CvdGenericCommandHandler::CvdHelpBinPath(const std::string& subcmd,
                                         const cvd_common::Envs& envs) const {
  auto tool_dir_path = envs.at(kAndroidHostOut);
  if (!DirectoryExists(tool_dir_path + "/bin")) {
    tool_dir_path =
        android::base::Dirname(android::base::GetExecutableDirectory());
  }
  auto bin_path_base = CF_EXPECT(GetBin(subcmd, tool_dir_path));
  // no need of executable directory. Will look up by PATH
  // bin_path_base is like ln, mkdir, etc.
  return BinPathInfo{
      .bin_ = bin_path_base,
      .bin_path_ = tool_dir_path.append("/bin/").append(bin_path_base),
      .host_artifacts_path_ = envs.at(kAndroidHostOut)};
}

/*
 * commands like ln, mkdir, clear
 *  -> bin, bin, system_wide_home, N/A, cmd_args, envs
 *
 * help command
 *  -> android_out/bin, bin, system_wide_home, android_out, cmd_args, envs
 *
 * non-help command
 *  -> group->a/o/bin, bin, group->home, group->android_out, cmd_args, envs
 *
 */
Result<CvdGenericCommandHandler::ExtractedInfo>
CvdGenericCommandHandler::ExtractInfo(const RequestWithStdio& request) const {
  auto result_opt = request.Credentials();
  CF_EXPECT(result_opt != std::nullopt);
  const uid_t uid = result_opt->uid;

  auto [subcmd, cmd_args] = ParseInvocation(request.Message());
  CF_EXPECT(Contains(command_to_binary_map_, subcmd));

  cvd_common::Envs envs =
      cvd_common::ConvertToEnvs(request.Message().command_request().env());
  const auto& selector_opts =
      request.Message().command_request().selector_opts();
  const auto selector_args = cvd_common::ConvertToArgs(selector_opts.args());
  CF_EXPECT(Contains(envs, kAndroidHostOut) &&
            DirectoryExists(envs.at(kAndroidHostOut)));

  std::unordered_set<std::string> non_cvd_op{"clear", "mkdir", "ln"};
  if (Contains(non_cvd_op, subcmd) || IsHelpSubcmd(cmd_args)) {
    const auto [bin, bin_path, host_artifacts_path] =
        Contains(non_cvd_op, subcmd) ? CF_EXPECT(NonCvdBinPath(subcmd, envs))
                                     : CF_EXPECT(CvdHelpBinPath(subcmd, envs));
    return ExtractedInfo{
        .invocation_info =
            CommandInvocationInfo{
                .command = subcmd,
                .bin = bin,
                .bin_path = bin_path,
                .home = CF_EXPECT(SystemWideUserHome(uid)),
                .host_artifacts_path = envs.at(kAndroidHostOut),
                .uid = uid,
                .args = cmd_args,
                .envs = envs},
        .group = std::nullopt};
  }

  auto instance_group =
      CF_EXPECT(instance_manager_.SelectGroup(selector_args, envs, uid));
  auto android_host_out = instance_group.HostArtifactsPath();
  auto home = instance_group.HomeDir();
  auto bin = CF_EXPECT(GetBin(subcmd, android_host_out));
  auto bin_path = ConcatToString(android_host_out, "/bin/", bin);
  CommandInvocationInfo result = {.command = subcmd,
                                  .bin = bin,
                                  .bin_path = bin_path,
                                  .home = home,
                                  .host_artifacts_path = android_host_out,
                                  .uid = uid,
                                  .args = cmd_args,
                                  .envs = envs};
  result.envs["HOME"] = home;
  return ExtractedInfo{.invocation_info = result, .group = instance_group};
}

Result<std::string> CvdGenericCommandHandler::GetBin(
    const std::string& subcmd) const {
  CF_EXPECT(Contains(command_to_binary_map_, subcmd));
  const auto& bin_name = command_to_binary_map_.at(subcmd);
  CF_EXPECT(bin_name != kBinGeneratedAtRuntime);
  return bin_name;
}

Result<std::string> CvdGenericCommandHandler::GetBin(
    const std::string& subcmd, const std::string& host_artifacts_path) const {
  CF_EXPECT(Contains(command_to_binary_map_, subcmd));
  const auto& bin_name = command_to_binary_map_.at(subcmd);
  if (bin_name != kBinGeneratedAtRuntime) {
    return GetBin(subcmd);
  }
  std::string calculated_bin_name =
      CF_EXPECT(host_tool_target_manager_.ExecBaseName(
          {.artifacts_path = host_artifacts_path, .op = subcmd}));
  return calculated_bin_name;
}

std::unique_ptr<CvdServerHandler> NewCvdGenericCommandHandler(
    InstanceManager& instance_manager, SubprocessWaiter& subprocess_waiter,
    HostToolTargetManager& host_tool_target_manager) {
  return std::unique_ptr<CvdServerHandler>(new CvdGenericCommandHandler(
      instance_manager, subprocess_waiter, host_tool_target_manager));
}

}  // namespace cuttlefish
