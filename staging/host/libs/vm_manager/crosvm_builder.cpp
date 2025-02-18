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
#include "host/libs/vm_manager/crosvm_cpu.h"

namespace cuttlefish {
namespace {

std::string MacCrosvmArgument(std::optional<std::string_view> mac) {
  return mac.has_value() ? fmt::format(",mac={}", mac.value()) : "";
}

std::string PciCrosvmArgument(std::optional<pci::Address> pci) {
  return pci.has_value() ? fmt::format(",pci-address={}", pci.value().Id())
                         : "";
}

}  // namespace

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

Result<void> CrosvmBuilder::AddCpus(size_t cpus,
                                    const std::string& vcpu_config_path) {
  if (!vcpu_config_path.empty()) {
    Json::Value vcpu_config_json = CF_EXPECT(LoadFromFile(vcpu_config_path));

    CF_EXPECT(AddCpus(vcpu_config_json));
  } else {
    AddCpus(cpus);
  }
  return {};
}

Result<void> CrosvmBuilder::AddCpus(const Json::Value& vcpu_config_json) {
  std::vector<std::string> cpu_args =
      CF_EXPECT(CrosvmCpuArguments(vcpu_config_json));

  for (const std::string& cpu_arg : cpu_args) {
    command_.AddParameter(cpu_arg);
  }
  return {};
}

void CrosvmBuilder::AddCpus(size_t cpus) {
  command_.AddParameter("--cpus=", cpus);
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
void CrosvmBuilder::AddHvcSocket(const std::string& socket) {
  command_.AddParameter(
      "--serial=hardware=virtio-console,num=", ++hvc_num_,
      ",type=unix-stream,input-unix-stream=true,path=", socket);
}

void CrosvmBuilder::AddReadOnlyDisk(const std::string& path) {
  command_.AddParameter("--block=path=", path, ",ro=true");
}

void CrosvmBuilder::AddReadWriteDisk(const std::string& path) {
  command_.AddParameter("--block=path=", path);
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
void CrosvmBuilder::AddTap(const std::string& tap_name,
                           std::optional<std::string_view> mac,
                           const std::optional<pci::Address>& pci) {
  command_.AddParameter("--net=tap-name=", tap_name, MacCrosvmArgument(mac),
                        PciCrosvmArgument(pci));
}
#endif

void CrosvmBuilder::AddVhostUser(const std::string& type,
                                 const std::string& socket_path,
                                 int max_queue_size) {
  command_.AddParameter("--vhost-user=type=", type, ",socket=", socket_path,
                        ",max-queue-size=", max_queue_size);
}

int CrosvmBuilder::HvcNum() { return hvc_num_; }

Command& CrosvmBuilder::Cmd() { return command_; }

}  // namespace cuttlefish
