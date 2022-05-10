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

#include "common/libs/utils/network.h"
#include "common/libs/utils/subprocess.h"

namespace cuttlefish {

CrosvmBuilder::CrosvmBuilder() : command_("crosvm") {
  command_.AddParameter("run");
}

void CrosvmBuilder::SetBinary(const std::string& binary) {
  command_.SetExecutableAndName(binary);
}

void CrosvmBuilder::AddControlSocket(const std::string& control_socket) {
  // Store this value so it persists after std::move(this->Cmd())
  auto crosvm = command_.Executable();
  command_.SetStopper([crosvm, control_socket](Subprocess* proc) {
    Command stop_cmd(crosvm);
    stop_cmd.AddParameter("stop");
    stop_cmd.AddParameter(control_socket);
    if (stop_cmd.Start().Wait() == 0) {
      return StopperResult::kStopSuccess;
    }
    LOG(WARNING) << "Failed to stop VMM nicely, attempting to KILL";
    return KillSubprocess(proc) == StopperResult::kStopSuccess
               ? StopperResult::kStopCrash
               : StopperResult::kStopFailure;
  });
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

SharedFD CrosvmBuilder::AddTap(const std::string& tap_name) {
  auto tap_fd = OpenTapInterface(tap_name);
  if (tap_fd->IsOpen()) {
    command_.AddParameter("--tap-fd=", tap_fd);
  } else {
    LOG(ERROR) << "Unable to connect to \"" << tap_name
               << "\": " << tap_fd->StrError();
  }
  return tap_fd;
}

int CrosvmBuilder::HvcNum() { return hvc_num_; }

Command& CrosvmBuilder::Cmd() { return command_; }

}  // namespace cuttlefish
