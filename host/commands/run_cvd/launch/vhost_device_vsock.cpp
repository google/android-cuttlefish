//
// Copyright (C) 2023 The Android Open Source Project
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

#include "vhost_device_vsock.h"

#include "host/commands/run_cvd/launch/launch.h"

#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

#include <fruit/fruit.h>

#include "common/libs/utils/files.h"
#include "common/libs/utils/result.h"
#include "host/commands/run_cvd/launch/log_tee_creator.h"
#include "host/libs/config/command_source.h"
#include "host/libs/config/cuttlefish_config.h"
#include "host/libs/config/known_paths.h"
#include "host/libs/vm_manager/vm_manager.h"

namespace cuttlefish {

VhostDeviceVsock::VhostDeviceVsock(
    LogTeeCreator& log_tee, const CuttlefishConfig::InstanceSpecific& instance,
    const CuttlefishConfig& cfconfig)
    : log_tee_(log_tee), instance_(instance), cfconfig_(cfconfig) {}

Result<std::vector<MonitorCommand>> VhostDeviceVsock::Commands() {
  auto instances = cfconfig_.Instances();
  if (instance_.serial_number() != instances[0].serial_number()) {
    return {};
  }

  Command command(ProcessRestarterBinary());
  command.AddParameter("-when_killed");
  command.AddParameter("-when_dumped");
  command.AddParameter("-when_exited_with_failure");
  command.AddParameter("--");
  command.AddParameter(HostBinaryPath("vhost_device_vsock"));
  command.AddEnvironmentVariable("RUST_BACKTRACE", "1");
  command.AddEnvironmentVariable("RUST_LOG", "debug");

  for (auto i : instances) {
    std::string isolation_groups = "";
    if (i.vsock_guest_group().length()) {
      isolation_groups = ",groups=" + i.vsock_guest_group();
    }

    auto param = fmt::format(
        "guest-cid={0},socket=/tmp/vsock_{0}_{1}/vhost.socket,uds-path=/tmp/"
        "vsock_{0}_{1}/vm.vsock{2}",
        i.vsock_guest_cid(), getuid(), isolation_groups);
    command.AddParameter("--vm");
    command.AddParameter(param);
    LOG(INFO) << "VhostDeviceVsock::vhost param is:" << param;
  }

  std::vector<MonitorCommand> commands;
  commands.emplace_back(std::move(
      CF_EXPECT(log_tee_.CreateLogTee(command, "vhost_device_vsock"))));
  commands.emplace_back(std::move(command));
  return commands;
}

std::string VhostDeviceVsock::Name() const { return "VhostDeviceVsock"; }

bool VhostDeviceVsock::Enabled() const { return instance_.vhost_user_vsock(); }

Result<void> VhostDeviceVsock::WaitForAvailability() const {
  if (Enabled()) {
    CF_EXPECT(WaitForUnixSocket(
        fmt::format("/tmp/vsock_{0}_{1}/vm.vsock", instance_.vsock_guest_cid(),
                    std::to_string(getuid())),
        30));
  }
  return {};
}

fruit::Component<fruit::Required<const CuttlefishConfig, LogTeeCreator,
                                 const CuttlefishConfig::InstanceSpecific>>
VhostDeviceVsockComponent() {
  return fruit::createComponent()
      .addMultibinding<vm_manager::VmmDependencyCommand, VhostDeviceVsock>()
      .addMultibinding<CommandSource, VhostDeviceVsock>()
      .addMultibinding<SetupFeature, VhostDeviceVsock>();
}

}  // namespace cuttlefish
