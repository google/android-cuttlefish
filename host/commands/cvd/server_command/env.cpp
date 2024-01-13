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

#include "host/commands/cvd/server_command/env.h"

#include <android-base/strings.h>

#include <iostream>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include "common/libs/fs/shared_buf.h"
#include "common/libs/utils/contains.h"
#include "common/libs/utils/subprocess.h"
#include "host/commands/cvd/flag.h"
#include "host/commands/cvd/selector/instance_group_record.h"
#include "host/commands/cvd/selector/instance_record.h"
#include "host/commands/cvd/selector/selector_constants.h"
#include "host/commands/cvd/server_command/server_handler.h"
#include "host/commands/cvd/server_command/utils.h"
#include "host/commands/cvd/types.h"

namespace cuttlefish {

class CvdEnvCommandHandler : public CvdServerHandler {
 public:
  CvdEnvCommandHandler(InstanceManager& instance_manager,
                       SubprocessWaiter& subprocess_waiter)
      : instance_manager_{instance_manager},
        subprocess_waiter_(subprocess_waiter),
        cvd_env_operations_{"env"} {}

  Result<bool> CanHandle(const RequestWithStdio& request) const {
    auto invocation = ParseInvocation(request.Message());
    return Contains(cvd_env_operations_, invocation.command);
  }

  Result<cvd::Response> Handle(const RequestWithStdio& request) override {
    std::unique_lock interrupt_lock(interruptible_);
    CF_EXPECT(!interrupted_, "Interrupted");
    CF_EXPECT(CanHandle(request));
    CF_EXPECT(VerifyPrecondition(request));
    const uid_t uid = request.Credentials()->uid;
    cvd_common::Envs envs =
        cvd_common::ConvertToEnvs(request.Message().command_request().env());

    auto [_, subcmd_args] = ParseInvocation(request.Message());

    /*
     * cvd_env --help only. Not --helpxml, etc.
     *
     * Otherwise, IsHelpSubcmd() should be used here instead.
     */
    auto help_flag = CvdFlag("help", false);
    cvd_common::Args subcmd_args_copy{subcmd_args};
    auto help_parse_result = help_flag.CalculateFlag(subcmd_args_copy);
    bool is_help = help_parse_result.ok() && (*help_parse_result);

    Command command =
        is_help ? CF_EXPECT(HelpCommand(request, subcmd_args, envs))
                : CF_EXPECT(NonHelpCommand(request, uid, subcmd_args, envs));
    CF_EXPECT(subprocess_waiter_.Setup(command.Start()));
    interrupt_lock.unlock();

    auto infop = CF_EXPECT(subprocess_waiter_.Wait());
    return ResponseFromSiginfo(infop);
  }

  Result<void> Interrupt() override {
    std::scoped_lock interrupt_lock(interruptible_);
    interrupted_ = true;
    CF_EXPECT(subprocess_waiter_.Interrupt());
    return {};
  }

  cvd_common::Args CmdList() const override {
    return cvd_common::Args(cvd_env_operations_.begin(),
                            cvd_env_operations_.end());
  }

 private:
  Result<Command> HelpCommand(const RequestWithStdio& request,
                              const cvd_common::Args& subcmd_args,
                              const cvd_common::Envs& envs) {
    CF_EXPECT(Contains(envs, kAndroidHostOut));
    return CF_EXPECT(
        ConstructCvdHelpCommand(kCvdEnvBin, envs, subcmd_args, request));
  }

  Result<Command> NonHelpCommand(const RequestWithStdio& request,
                                 const uid_t uid,
                                 const cvd_common::Args& subcmd_args,
                                 const cvd_common::Envs& envs) {
    const auto& selector_opts =
        request.Message().command_request().selector_opts();
    const auto selector_args = cvd_common::ConvertToArgs(selector_opts.args());

    auto instance =
        CF_EXPECT(instance_manager_.SelectInstance(selector_args, envs, uid));
    const auto& instance_group = instance.ParentGroup();
    const auto& home = instance_group.HomeDir();

    const auto& android_host_out = instance_group.HostArtifactsPath();
    auto cvd_env_bin_path =
        ConcatToString(android_host_out, "/bin/", kCvdEnvBin);
    const auto& internal_device_name = instance.InternalDeviceName();

    cvd_common::Args cvd_env_args{internal_device_name};
    cvd_env_args.insert(cvd_env_args.end(), subcmd_args.begin(),
                        subcmd_args.end());

    return CF_EXPECT(
        ConstructCvdGenericNonHelpCommand({.bin_file = kCvdEnvBin,
                                           .envs = envs,
                                           .cmd_args = cvd_env_args,
                                           .android_host_out = android_host_out,
                                           .home = home,
                                           .verbose = true},
                                          request));
  }

  InstanceManager& instance_manager_;
  SubprocessWaiter& subprocess_waiter_;
  std::mutex interruptible_;
  bool interrupted_ = false;
  std::vector<std::string> cvd_env_operations_;

  static constexpr char kCvdEnvBin[] = "cvd_internal_env";
};

std::unique_ptr<CvdServerHandler> NewCvdEnvCommandHandler(
    InstanceManager& instance_manager, SubprocessWaiter& subprocess_waiter) {
  return std::unique_ptr<CvdServerHandler>(
      new CvdEnvCommandHandler(instance_manager, subprocess_waiter));
}

}  // namespace cuttlefish
