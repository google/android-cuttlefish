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

#include "cuttlefish/host/commands/cvd/cli/commands/snapshot.h"

#include <signal.h>  // IWYU pragma: keep
#include <stdlib.h>

#include <iostream>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

#include "absl/log/log.h"
#include "android-base/file.h"

#include "cuttlefish/common/libs/utils/files.h"
#include "cuttlefish/host/commands/cvd/cli/command_request.h"
#include "cuttlefish/host/commands/cvd/cli/commands/host_tool_target.h"
#include "cuttlefish/host/commands/cvd/cli/selector/selector.h"
#include "cuttlefish/host/commands/cvd/cli/utils.h"
#include "cuttlefish/host/commands/cvd/instances/instance_manager.h"
#include "cuttlefish/host/commands/cvd/instances/local_instance_group.h"
#include "cuttlefish/host/commands/cvd/utils/common.h"
#include "cuttlefish/process/command.h"
#include "cuttlefish/result/result.h"

namespace cuttlefish {
namespace {

constexpr char kSummaryHelpText[] =
    "Suspend/resume the cuttlefish device, or take snapshot of the device";

constexpr char kDetailedHelpText[] =
    R"(Cuttlefish Virtual Device (CVD) CLI.

Suspend/resume a Cuttlefish instance group, or take a snapshot of it.
All three commands operate on the entire selected instance group.

Usage:
  cvd [selector flags] suspend [flags] [--help]
  cvd [selector flags] resume [flags] [--help]
  cvd [selector flags] snapshot_take --snapshot_path=<path> [flags] [--help]

Selector Flags:
  --group_name=<name>         Name of the instance group
  --instance_name=<name>      Instance name used to select its containing group
                              (alias: --instance_names)

Common Flags (suspend, resume, snapshot_take):
  --wait_for_launcher=<secs>  Seconds to wait to connect to the launcher monitor
                              (default: 30)

snapshot_take Flags:
  --snapshot_path=<path>      Directory to save snapshot files (required; must
                              not already exist unless --force is set)
  --force                     Delete --snapshot_path first if it already exists
  --auto_suspend              Suspend before and resume after taking snapshot
  --[no]cleanup_snapshot_path Clean up --snapshot_path on failure
                              (default: true)

Crosvm Launch Flags (pass to `cvd create` / `cvd start`):
  --snapshot_compatible       Validate at launch that single-instance Crosvm
                              flags support snapshot/suspend/resume (does not
                              set them automatically). Currently requires:
    --enable_virtiofs=false   Disable virtio-fs (crosvm does not support
                              virtio_sleep for virtio-fs)
    --enable_usb=false        Disable USB passthrough
    --gpu_mode=guest_swiftshader
                              Use 2D guest SwiftShader rendering (3D GPU
                              snapshot restore is not supported)

VM Manager Support:
  Only Crosvm is currently supported. QEMU is not supported for suspend,
  resume, or snapshot_take.

Examples:
  # Launch a snapshot-compatible Crosvm device:
  cvd create --snapshot_compatible --enable_virtiofs=false \
    --enable_usb=false --gpu_mode=guest_swiftshader

  # Take a snapshot (auto-suspending and resuming the running device):
  cvd snapshot_take --auto_suspend --snapshot_path=/tmp/cf_snapshot

  # Restore a stopped group from a snapshot:
  cvd start --snapshot_path=/tmp/cf_snapshot)";

Result<std::string> GetBin(const std::string& host_artifacts_path) {
  return CF_EXPECT(HostToolTarget(host_artifacts_path).GetSnapshotBinName());
}

}  // namespace

CvdSnapshotCommandHandler::CvdSnapshotCommandHandler(
    InstanceManager& instance_manager)
    : instance_manager_{instance_manager} {}

Result<void> CvdSnapshotCommandHandler::Handle(const CommandRequest& request) {
  std::string subcmd = request.Subcommand();
  std::vector<std::string> subcmd_args = request.SubcommandArguments();

  std::stringstream ss;
  for (const auto& arg : subcmd_args) {
    ss << arg << " ";
  }
  VLOG(0) << "Calling new handler with " << subcmd << ": " << ss.str();

  // may modify subcmd_args by consuming in parsing
  Command command =
      CF_EXPECT(GenerateCommand(request, subcmd, subcmd_args, request.Env()));

  // NOLINTNEXTLINE(misc-include-cleaner)
  siginfo_t infop = CF_EXPECT(command.Start().Wait(WEXITED));

  CF_EXPECT(CheckProcessExitedNormally(infop));
  return {};
}

std::vector<std::string> CvdSnapshotCommandHandler::CmdList() const {
  return {"suspend", "resume", "snapshot_take"};
}

std::string CvdSnapshotCommandHandler::SummaryHelp() const {
  return kSummaryHelpText;
}

Result<std::string> CvdSnapshotCommandHandler::DetailedHelp(
    const CommandRequest& request) {
  return kDetailedHelpText;
}

Result<Command> CvdSnapshotCommandHandler::GenerateCommand(
    const CommandRequest& request, const std::string& subcmd,
    std::vector<std::string>& subcmd_args,
    std::unordered_map<std::string, std::string> envs) {
  // create a string that is comma-separated instance IDs
  auto instance_group =
      CF_EXPECT(selector::SelectGroup(instance_manager_, request));

  const auto& home = instance_group.HomeDir();
  const auto& android_host_out = instance_group.HostArtifactsPath();
  auto cvd_snapshot_bin_path =
      android_host_out + "/bin/" + CF_EXPECT(GetBin(android_host_out));
  const std::string& snapshot_util_cmd = subcmd;
  std::vector<std::string> cvd_snapshot_args{"--subcmd=" + snapshot_util_cmd};
  cvd_snapshot_args.insert(cvd_snapshot_args.end(), subcmd_args.begin(),
                           subcmd_args.end());
  // This helps snapshot_util find CuttlefishConfig and figure out
  // the instance ids
  envs["HOME"] = home;
  envs[kAndroidHostOut] = android_host_out;
  envs[kAndroidSoongHostOut] = android_host_out;

  std::cerr << "HOME=" << home << " " << kAndroidHostOut << "="
            << android_host_out << " " << kAndroidSoongHostOut << "="
            << android_host_out << " " << cvd_snapshot_bin_path << " ";
  for (const auto& arg : cvd_snapshot_args) {
    std::cerr << arg << " ";
  }

  ConstructCommandParam construct_cmd_param{
      .bin_path = cvd_snapshot_bin_path,
      .home = home,
      .args = cvd_snapshot_args,
      .envs = envs,
      .working_dir = CurrentDirectory(),
      .command_name = android::base::Basename(cvd_snapshot_bin_path),
  };
  Command command = CF_EXPECT(ConstructCommand(construct_cmd_param));
  return command;
}

}  // namespace cuttlefish
