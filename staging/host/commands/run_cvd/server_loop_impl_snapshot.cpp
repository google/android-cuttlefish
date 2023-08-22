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

#include "host/commands/run_cvd/server_loop_impl.h"

#include <sstream>

#include <android-base/file.h>

#include "common/libs/fs/shared_fd.h"
#include "common/libs/utils/contains.h"
#include "common/libs/utils/files.h"
#include "common/libs/utils/result.h"
#include "host/commands/run_cvd/runner_defs.h"
#include "host/libs/command_util/util.h"
#include "host/libs/vm_manager/crosvm_manager.h"
#include "host/libs/vm_manager/qemu_manager.h"
#include "run_cvd.pb.h"

namespace cuttlefish {
namespace run_cvd_impl {

std::unordered_map<std::string, std::string>
ServerLoopImpl::InitializeVmToControlSockPath(
    const CuttlefishConfig::InstanceSpecific& instance) {
  return std::unordered_map<std::string, std::string>{
      // TODO(kwstephenkim): add the following two lines to support QEMU
      // {QemuManager::name(),
      // instance.PerInstanceInternalUdsPath("qemu_monitor.sock")},
      {vm_manager::CrosvmManager::name(),
       instance.PerInstanceInternalUdsPath("crosvm_control.sock")},
  };
}

static std::string SubtoolPath(const std::string& subtool_name) {
  auto my_own_dir = android::base::GetExecutableDirectory();
  std::stringstream subtool_path_stream;
  subtool_path_stream << my_own_dir << "/" << subtool_name;
  auto subtool_path = subtool_path_stream.str();
  if (my_own_dir.empty() || !FileExists(subtool_path)) {
    return HostBinaryPath(subtool_name);
  }
  return subtool_path;
}

static Result<void> SuspendCrosvm(const std::string& vm_sock_path) {
  const auto crosvm_bin_path = SubtoolPath("crosvm");
  std::vector<std::string> command_args{crosvm_bin_path, "suspend",
                                        vm_sock_path, "--full"};
  auto infop = CF_EXPECT(Execute(command_args, SubprocessOptions(), WEXITED));
  CF_EXPECT_EQ(infop.si_code, CLD_EXITED);
  CF_EXPECTF(infop.si_status == 0, "crosvm suspend returns non zero code {}",
             infop.si_status);
  return {};
}

static Result<void> ResumeCrosvm(const std::string& vm_sock_path) {
  const auto crosvm_bin_path = SubtoolPath("crosvm");
  std::vector<std::string> command_args{crosvm_bin_path, "resume", vm_sock_path,
                                        "--full"};
  auto infop = CF_EXPECT(Execute(command_args, SubprocessOptions(), WEXITED));
  CF_EXPECT_EQ(infop.si_code, CLD_EXITED);
  CF_EXPECTF(infop.si_status == 0, "crosvm resume returns non zero code {}",
             infop.si_status);
  return {};
}

Result<void> ServerLoopImpl::SuspendGuest() {
  const auto vm_name = config_.vm_manager();
  CF_EXPECTF(Contains(vm_name_to_control_sock_, vm_name),
             "vm_manager \"{}\" is not supported for suspend yet.", vm_name);
  const auto& vm_sock_path = vm_name_to_control_sock_.at(vm_name);
  if (vm_name == vm_manager::CrosvmManager::name()) {
    return SuspendCrosvm(vm_sock_path);
  } else {
    return CF_ERR("The vm_manager " + vm_name + " is not supported yet");
  }
}

Result<void> ServerLoopImpl::ResumeGuest() {
  const auto vm_name = config_.vm_manager();
  CF_EXPECTF(Contains(vm_name_to_control_sock_, vm_name),
             "vm_manager \"{}\" is not supported for resume yet.", vm_name);
  const auto& vm_sock_path = vm_name_to_control_sock_.at(vm_name);
  if (vm_name == vm_manager::CrosvmManager::name()) {
    return ResumeCrosvm(vm_sock_path);
  } else {
    return CF_ERR("The vm_manager " + vm_name + " is not supported yet");
  }
}

Result<void> ServerLoopImpl::HandleSuspend(const std::string& serialized_data,
                                           ProcessMonitor& process_monitor) {
  run_cvd::ExtendedLauncherAction extended_action;
  CF_EXPECT(extended_action.ParseFromString(serialized_data),
            "Failed to load ExtendedLauncherAction proto.");
  CF_EXPECT_EQ(extended_action.actions_case(),
               run_cvd::ExtendedLauncherAction::ActionsCase::kSuspend);
  // right order: guest -> host
  LOG(DEBUG) << "Suspending the guest..";
  CF_EXPECT(SuspendGuest());
  LOG(DEBUG) << "The guest is suspended.";
  CF_EXPECT(process_monitor.SuspendMonitoredProcesses(),
            "Failed to suspend host processes.");
  LOG(DEBUG) << "The host processes are suspended.";
  return {};
}

Result<void> ServerLoopImpl::HandleResume(const std::string& serialized_data,
                                          ProcessMonitor& process_monitor) {
  run_cvd::ExtendedLauncherAction extended_action;
  CF_EXPECT(extended_action.ParseFromString(serialized_data),
            "Failed to load ExtendedLauncherAction proto.");
  CF_EXPECT_EQ(extended_action.actions_case(),
               run_cvd::ExtendedLauncherAction::ActionsCase::kResume);
  // right order: host -> guest
  CF_EXPECT(process_monitor.ResumeMonitoredProcesses(),
            "Failed to resume host processes.");
  LOG(DEBUG) << "The host processes are resumed.";
  LOG(DEBUG) << "Resuming the guest..";
  CF_EXPECT(ResumeGuest());
  LOG(DEBUG) << "The guest resumed.";
  return {};
}

Result<void> ServerLoopImpl::HandleSnapshotTake(
    const std::string& serialized_data) {
  run_cvd::ExtendedLauncherAction extended_action;
  CF_EXPECT(extended_action.ParseFromString(serialized_data),
            "Failed to load ExtendedLauncherAction proto.");
  CF_EXPECT_EQ(extended_action.actions_case(),
               run_cvd::ExtendedLauncherAction::ActionsCase::kSnapshotTake);
  // implement snapshot take
  std::vector<std::string> path_to_snapshots;
  for (const auto& path : extended_action.snapshot_take().snapshot_path()) {
    path_to_snapshots.push_back(path);
  }
  CF_EXPECT_EQ(path_to_snapshots.size(), 1);
  const auto& path_to_snapshot = path_to_snapshots.front();
  LOG(DEBUG) << "run_cvd server loop will take snapshot to "
             << path_to_snapshot;
  return {};
}

}  // namespace run_cvd_impl
}  // namespace cuttlefish
