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

#include <sstream>
#include <string>
#include <vector>

#include "common/libs/fs/shared_buf.h"
#include "common/libs/utils/contains.h"
#include "common/libs/utils/subprocess.h"
#include "host/commands/cvd/common_utils.h"
#include "host/commands/cvd/flag.h"
#include "host/commands/cvd/selector/instance_group_record.h"
#include "host/commands/cvd/server_command/host_tool_target_manager.h"
#include "host/commands/cvd/server_command/server_handler.h"
#include "host/commands/cvd/server_command/utils.h"
#include "host/commands/cvd/types.h"

namespace cuttlefish {
namespace {

constexpr char kSummaryHelpText[] =
    "Suspend/resume the cuttlefish device, or take snapshot of the device";

constexpr char kDetailedHelpText[] =
    R"(Cuttlefish Virtual Device (CVD) CLI.

Suspend/resume the cuttlefish device, or take snapshot of the device

usage: cvd [selector flags] suspend/resume/snapshot_take [--help]

Common:
  Selector Flags:
    --group_name=<name>       The name of the instance group
    --snapshot_path=<path>>   Directory that contains saved snapshot files

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
                            HostToolTargetManager& host_tool_target_manager)
      : instance_manager_{instance_manager},
        host_tool_target_manager_(host_tool_target_manager),
        cvd_snapshot_operations_{"suspend", "resume", "snapshot_take"} {}

  Result<bool> CanHandle(const RequestWithStdio& request) const override {
    auto invocation = ParseInvocation(request.Message());
    return Contains(cvd_snapshot_operations_, invocation.command);
  }

  Result<cvd::Response> Handle(const RequestWithStdio& request) override {
    CF_EXPECT(CanHandle(request));
    CF_EXPECT(VerifyPrecondition(request));
    cvd_common::Envs envs = request.Envs();

    auto [subcmd, subcmd_args] = ParseInvocation(request.Message());

    std::stringstream ss;
    for (const auto& arg : subcmd_args) {
      ss << arg << " ";
    }
    LOG(DEBUG) << "Calling new handler with " << subcmd << ": " << ss.str();

    // may modify subcmd_args by consuming in parsing
    Command command =
        CF_EXPECT(GenerateCommand(request, subcmd, subcmd_args, envs));

    siginfo_t infop;
    command.Start().Wait(&infop, WEXITED);

    return ResponseFromSiginfo(infop);
  }

  cvd_common::Args CmdList() const override {
    return cvd_common::Args(cvd_snapshot_operations_.begin(),
                            cvd_snapshot_operations_.end());
  }

  Result<std::string> SummaryHelp() const override { return kSummaryHelpText; }

  bool ShouldInterceptHelp() const override { return true; }

  Result<std::string> DetailedHelp(std::vector<std::string>&) const override {
    return kDetailedHelpText;
  }

 private:
  Result<Command> GenerateCommand(const RequestWithStdio& request,
                                  const std::string& subcmd,
                                  cvd_common::Args& subcmd_args,
                                  cvd_common::Envs envs) {
    const auto selector_args = request.SelectorArgs();

    // create a string that is comma-separated instance IDs
    auto instance_group =
        CF_EXPECT(instance_manager_.SelectGroup(selector_args, envs));

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
  HostToolTargetManager& host_tool_target_manager_;
  std::vector<std::string> cvd_snapshot_operations_;
};

}  // namespace

std::unique_ptr<CvdServerHandler> NewCvdSnapshotCommandHandler(
    InstanceManager& instance_manager,
    HostToolTargetManager& host_tool_target_manager) {
  return std::unique_ptr<CvdServerHandler>(new CvdSnapshotCommandHandler(
      instance_manager, host_tool_target_manager));
}

}  // namespace cuttlefish
