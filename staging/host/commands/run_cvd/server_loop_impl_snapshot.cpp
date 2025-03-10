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
#include <string>

#include <android-base/file.h>

#include "common/libs/fs/shared_buf.h"
#include "common/libs/fs/shared_fd.h"
#include "common/libs/utils/contains.h"
#include "common/libs/utils/files.h"
#include "common/libs/utils/json.h"
#include "common/libs/utils/result.h"
#include "host/commands/run_cvd/runner_defs.h"
#include "host/libs/command_util/snapshot_utils.h"
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
      {vm_manager::CrosvmManager::name(), instance.CrosvmSocketPath()},
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

Result<void> ServerLoopImpl::TakeCrosvmGuestSnapshot(
    const Json::Value& meta_json) {
  const auto snapshots_parent_dir =
      CF_EXPECT(InstanceGuestSnapshotPath(meta_json, instance_.id()));
  const auto crosvm_bin = config_.crosvm_binary();
  const auto control_socket_path =
      CF_EXPECT(VmControlSocket(), "Failed to find crosvm control.sock path.");
  const std::string snapshot_guest_param =
      snapshots_parent_dir + "/" + kGuestSnapshotBase;
  std::vector<std::string> crosvm_command_args{crosvm_bin, "snapshot", "take",
                                               snapshot_guest_param,
                                               control_socket_path};
  std::stringstream ss;
  LOG(DEBUG) << "Running the following command to take snapshot..." << std::endl
             << "  ";
  for (const auto& arg : crosvm_command_args) {
    LOG(DEBUG) << arg << " ";
  }
  CF_EXPECT(Execute(crosvm_command_args) == 0,
            "Executing crosvm command failed");
  LOG(DEBUG) << "Guest snapshot for instance #" << instance_.id()
             << " should have been stored in " << snapshots_parent_dir;
  return {};
}

/*
 * Parse json file at json_path, and take guest snapshot
 */
Result<void> ServerLoopImpl::TakeGuestSnapshot(const std::string& vm_manager,
                                               const std::string& json_path) {
  // common code across vm_manager
  CF_EXPECTF(FileExists(json_path), "{} must exist but does not.", json_path);
  SharedFD json_fd = SharedFD::Open(json_path, O_RDONLY);
  CF_EXPECTF(json_fd->IsOpen(), "Failed to open {}", json_path);
  std::string json_contents;
  CF_EXPECT_GE(ReadAll(json_fd, &json_contents), 0,
               std::string("Failed to read from ") + json_path);
  Json::Value meta_json = CF_EXPECTF(
      ParseJson(json_contents), "Failed to parse json: \n{}", json_contents);
  CF_EXPECTF(vm_manager == "crosvm",
             "{}, which is not crosvm, is not yet supported.", vm_manager);
  CF_EXPECT(TakeCrosvmGuestSnapshot(meta_json),
            "TakeCrosvmGuestSnapshot() failed.");
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
  CF_EXPECT(TakeGuestSnapshot(config_.vm_manager(), path_to_snapshot),
            "Failed to take guest snapshot");
  return {};
}

}  // namespace run_cvd_impl
}  // namespace cuttlefish
