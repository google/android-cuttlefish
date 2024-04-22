//
// Copyright (C) 2021 The Android Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "host/libs/vm_manager/crosvm_builder.h"

#include <android-base/logging.h>

#include <string>
#include <vector>

#include "common/libs/utils/json.h"
#include "common/libs/utils/network.h"
#include "common/libs/utils/subprocess.h"
#include "host/libs/command_util/snapshot_utils.h"
#include "host/libs/config/cuttlefish_config.h"
#include "host/libs/config/known_paths.h"

namespace cuttlefish {

CrosvmBuilder::CrosvmBuilder() : command_("crosvm") {}

void CrosvmBuilder::ApplyProcessRestarter(
    const std::string& crosvm_binary, const std::string& first_time_argument,
    int exit_code) {
  command_.SetExecutableAndName(ProcessRestarterBinary());
  command_.AddParameter("-when_exited_with_code=", exit_code);
  command_.AddParameter("-ignore_sigtstp");
  if (!first_time_argument.empty()) {
    command_.AddParameter("-first_time_argument=", first_time_argument);
  }
  command_.AddParameter("--");
  command_.AddParameter(crosvm_binary);
  // Flag allows exit codes other than 0 or 1, must be before command argument
  command_.AddParameter("--extended-status");
}

void CrosvmBuilder::AddControlSocket(const std::string& control_socket,
                                     const std::string& executable_path) {
  auto stopper = [executable_path, control_socket]() {
    Command stop_cmd(executable_path);
    stop_cmd.AddParameter("stop");
    stop_cmd.AddParameter(control_socket);
    return stop_cmd.Start().Wait() == 0 ? StopperResult::kStopSuccess
                                        : StopperResult::kStopFailure;
  };
  command_.SetStopper(KillSubprocessFallback(stopper));
  command_.AddParameter("--socket=", control_socket);
}

void CrosvmBuilder::AddHvcSink() {
  command_.AddParameter("--serial=hardware=virtio-console,num=", ++hvc_num_,
                        ",type=sink");
}
void CrosvmBuilder::AddHvcReadOnly(const std::string& output, bool console) {
  command_.AddParameter("--serial=hardware=virtio-console,num=", ++hvc_num_,
                        ",type=file,path=", output,
                        console ? ",console=true" : "");
}
void CrosvmBuilder::AddHvcReadWrite(const std::string& output,
                                    const std::string& input) {
  command_.AddParameter("--serial=hardware=virtio-console,num=", ++hvc_num_,
                        ",type=file,path=", output, ",input=", input);
}

void CrosvmBuilder::AddReadOnlyDisk(const std::string& path) {
  command_.AddParameter("--disk=", path);
}

void CrosvmBuilder::AddReadWriteDisk(const std::string& path) {
  command_.AddParameter("--rwdisk=", path);
}

void CrosvmBuilder::AddSerialSink() {
  command_.AddParameter("--serial=hardware=serial,num=", ++serial_num_,
                        ",type=sink");
}
void CrosvmBuilder::AddSerialConsoleReadOnly(const std::string& output) {
  command_.AddParameter("--serial=hardware=serial,num=", ++serial_num_,
                        ",type=file,path=", output, ",earlycon=true");
}
void CrosvmBuilder::AddSerialConsoleReadWrite(const std::string& output,
                                              const std::string& input,
                                              bool earlycon) {
  command_.AddParameter("--serial=hardware=serial,num=", ++serial_num_,
                        ",type=file,path=", output, ",input=", input,
                        earlycon ? ",earlycon=true" : "");
}
void CrosvmBuilder::AddSerial(const std::string& output,
                              const std::string& input) {
  command_.AddParameter("--serial=hardware=serial,num=", ++serial_num_,
                        ",type=file,path=", output, ",input=", input);
}

#ifdef __linux__
SharedFD CrosvmBuilder::AddTap(const std::string& tap_name) {
  auto tap_fd = OpenTapInterface(tap_name);
  if (tap_fd->IsOpen()) {
    command_.AddParameter("--net=tap-fd=", tap_fd);
  } else {
    LOG(ERROR) << "Unable to connect to \"" << tap_name
               << "\": " << tap_fd->StrError();
  }
  return tap_fd;
}

SharedFD CrosvmBuilder::AddTap(const std::string& tap_name, const std::string& mac) {
  auto tap_fd = OpenTapInterface(tap_name);
  if (tap_fd->IsOpen()) {
    command_.AddParameter("--net=tap-fd=", tap_fd, ",mac=\"", mac, "\"");
  } else {
    LOG(ERROR) << "Unable to connect to \"" << tap_name
               << "\": " << tap_fd->StrError();
  }
  return tap_fd;
}
#endif

int CrosvmBuilder::HvcNum() { return hvc_num_; }

Result<void> CrosvmBuilder::SetToRestoreFromSnapshot(
    const std::string& snapshot_dir_path, const std::string& instance_id_in_str,
    const std::string& snapshot_name) {
  auto meta_info_json = CF_EXPECT(LoadMetaJson(snapshot_dir_path));
  const std::vector<std::string> selectors{kGuestSnapshotField,
                                           instance_id_in_str};
  const auto guest_snapshot_dir_suffix =
      CF_EXPECT(GetValue<std::string>(meta_info_json, selectors));
  // guest_snapshot_dir_suffix is a relative to
  // the snapshot_path
  const auto restore_path = snapshot_dir_path + "/" +
                            guest_snapshot_dir_suffix + "/" +
                            kGuestSnapshotBase + snapshot_name;
  command_.AddParameter("--restore=", restore_path);
  return {};
}

Command& CrosvmBuilder::Cmd() { return command_; }

}  // namespace cuttlefish
