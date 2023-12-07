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

#include "host/commands/cvd/server_command/snapshot.h"

#include <android-base/file.h>
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
#include "host/commands/cvd/selector/instance_database_types.h"
#include "host/commands/cvd/selector/instance_group_record.h"
#include "host/commands/cvd/selector/instance_record.h"
#include "host/commands/cvd/selector/selector_constants.h"
#include "host/commands/cvd/server_command/host_tool_target_manager.h"
#include "host/commands/cvd/server_command/server_handler.h"
#include "host/commands/cvd/server_command/utils.h"
#include "host/commands/cvd/types.h"

namespace cuttlefish {

static constexpr char kSnapshot[] =
    R"(Cuttlefish Virtual Device (CVD) CLI.

Suspend/resume the cuttlefish device, or take snapshot of the device

usage: cvd [selector flags] suspend/resume/snapshot_take [--help]

Common:
  Selector Flags:
    --group_name=<name>       The name of the instance group
    --snapshot_path=<path>>   Directory that contains saved snapshot files

  Args:
    --help                    print this message

Crosvm:
  --snapshot_compat           Tells the device to be snapshot-compatible
                              The device to be created is checked if it is
                              compatible with snapshot operations

QEMU:
  No QEMU-specific arguments at the moment

)";

class CvdSnapshotCommandHandler : public CvdServerHandler {
 public:
  CvdSnapshotCommandHandler(InstanceManager& instance_manager,
                            SubprocessWaiter& subprocess_waiter,
                            HostToolTargetManager& host_tool_target_manager)
      : instance_manager_{instance_manager},
        subprocess_waiter_(subprocess_waiter),
        host_tool_target_manager_(host_tool_target_manager),
        cvd_snapshot_operations_{"suspend", "resume", "snapshot_take"} {}

  Result<bool> CanHandle(const RequestWithStdio& request) const {
    auto invocation = ParseInvocation(request.Message());
    return Contains(cvd_snapshot_operations_, invocation.command);
  }

  Result<cvd::Response> Handle(const RequestWithStdio& request) override {
    std::unique_lock interrupt_lock(interruptible_);
    CF_EXPECT(!interrupted_, "Interrupted");
    CF_EXPECT(CanHandle(request));
    CF_EXPECT(VerifyPrecondition(request));
    const uid_t uid = request.Credentials()->uid;
    cvd_common::Envs envs =
        cvd_common::ConvertToEnvs(request.Message().command_request().env());

    auto [subcmd, subcmd_args] = ParseInvocation(request.Message());

    std::stringstream ss;
    for (const auto& arg : subcmd_args) {
      ss << arg << " ";
    }
    LOG(DEBUG) << "Calling new handler with " << subcmd << ": " << ss.str();

    auto help_flag = CvdFlag("help", false);
    cvd_common::Args subcmd_args_copy{subcmd_args};
    auto help_parse_result = help_flag.CalculateFlag(subcmd_args_copy);
    bool is_help = help_parse_result.ok() && (*help_parse_result);

    if (is_help) {
      auto help_response = CF_EXPECT(HandleHelp(request.Err()));
      return help_response;
    }

    // may modify subcmd_args by consuming in parsing
    Command command =
        CF_EXPECT(NonHelpCommand(request, uid, subcmd, subcmd_args, envs));
    SubprocessOptions options;
    CF_EXPECT(subprocess_waiter_.Setup(command.Start(options)));
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
    return cvd_common::Args(cvd_snapshot_operations_.begin(),
                            cvd_snapshot_operations_.end());
  }

 private:
  Result<cvd::Response> HandleHelp(const SharedFD& client_stderr) {
    std::string help_message(kSnapshot);
    help_message.append("\n");
    CF_EXPECT(WriteAll(client_stderr, help_message) == help_message.size(),
              "Failed to write the help message");
    cvd::Response response;
    response.mutable_command_response();
    response.mutable_status()->set_code(cvd::Status::OK);
    return response;
  }

  Result<Command> NonHelpCommand(const RequestWithStdio& request,
                                 const uid_t uid, const std::string& subcmd,
                                 cvd_common::Args& subcmd_args,
                                 cvd_common::Envs envs) {
    const auto& selector_opts =
        request.Message().command_request().selector_opts();
    const auto selector_args = cvd_common::ConvertToArgs(selector_opts.args());

    // create a string that is comma-separated instance IDs
    auto instance_group =
        CF_EXPECT(instance_manager_.SelectGroup(selector_args, envs, uid));

    const auto& home = instance_group.HomeDir();
    const auto& android_host_out = instance_group.HostArtifactsPath();
    auto cvd_snapshot_bin_path = android_host_out + "/bin/" +
                                 CF_EXPECT(GetBin(android_host_out, subcmd));
    const std::string& snapshot_util_cmd = subcmd;
    cvd_common::Args cvd_snapshot_args{"--subcmd=" + snapshot_util_cmd};
    cvd_snapshot_args.insert(cvd_snapshot_args.end(), subcmd_args.begin(),
                             subcmd_args.end());
    // This helps snapshot_util find CuttlefishConfig and figure out
    // the instance ids
    envs["HOME"] = home;
    envs[kAndroidHostOut] = android_host_out;
    envs[kAndroidSoongHostOut] = android_host_out;

    std::stringstream command_to_issue;
    command_to_issue << "HOME=" << home << " " << kAndroidHostOut << "="
                     << android_host_out << " " << kAndroidSoongHostOut << "="
                     << android_host_out << " " << cvd_snapshot_bin_path << " ";
    for (const auto& arg : cvd_snapshot_args) {
      command_to_issue << arg << " ";
    }
    WriteAll(request.Err(), command_to_issue.str());

    ConstructCommandParam construct_cmd_param{
        .bin_path = cvd_snapshot_bin_path,
        .home = home,
        .args = cvd_snapshot_args,
        .envs = envs,
        .working_dir = request.Message().command_request().working_directory(),
        .command_name = android::base::Basename(cvd_snapshot_bin_path),
        .in = request.In(),
        .out = request.Out(),
        .err = request.Err()};
    Command command = CF_EXPECT(ConstructCommand(construct_cmd_param));
    return command;
  }

  Result<std::string> GetBin(const std::string& host_artifacts_path,
                             const std::string& op) const {
    auto snapshot_bin = CF_EXPECT(host_tool_target_manager_.ExecBaseName({
        .artifacts_path = host_artifacts_path,
        .op = op,
    }));
    return snapshot_bin;
  }

  InstanceManager& instance_manager_;
  SubprocessWaiter& subprocess_waiter_;
  HostToolTargetManager& host_tool_target_manager_;
  std::mutex interruptible_;
  bool interrupted_ = false;
  std::vector<std::string> cvd_snapshot_operations_;
};

std::unique_ptr<CvdServerHandler> NewCvdSnapshotCommandHandler(
    InstanceManager& instance_manager, SubprocessWaiter& subprocess_waiter,
    HostToolTargetManager& host_tool_target_manager) {
  return std::unique_ptr<CvdServerHandler>(new CvdSnapshotCommandHandler(
      instance_manager, subprocess_waiter, host_tool_target_manager));
}

}  // namespace cuttlefish
