//
// Copyright (C) 2026 The Android Open Source Project
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

#include "cuttlefish/host/commands/run_cvd/launch/vhost_user_media_devices.h"

#include <fcntl.h>

#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

#include <fruit/component.h>
#include <fruit/fruit_forward_decls.h>
#include <fruit/macro.h>

#include "cuttlefish/common/libs/fs/shared_fd.h"
#include "cuttlefish/common/libs/utils/subprocess.h"
#include "cuttlefish/host/commands/run_cvd/launch/log_tee_creator.h"
#include "cuttlefish/host/libs/config/cuttlefish_config.h"
#include "cuttlefish/host/libs/config/known_paths.h"
#include "cuttlefish/host/libs/feature/command_source.h"
#include "cuttlefish/host/libs/feature/feature.h"
#include "cuttlefish/result/result.h"

namespace cuttlefish {
namespace {

using Subprocess::StdIOChannel::kStdErr;

Command NewCommand(const std::string& socket_path) {
  Command cmd(VhostUserMediaSimpleDeviceBinary());
  cmd.AddParameter("--socket-path=", socket_path);
  cmd.AddParameter("--verbosity=", "debug");
  cmd.RedirectStdIO(Subprocess::StdIOChannel::kStdOut,
                    SharedFD::Open("/dev/null", O_WRONLY));
  return cmd;
}

class VhostUserMediaDevices : public CommandSource {
 public:
  INJECT(VhostUserMediaDevices(const CuttlefishConfig::InstanceSpecific& instance, LogTeeCreator& log_tee)) : instance_(instance), log_tee_(log_tee) {}

  // CommandSource
  Result<std::vector<MonitorCommand>> Commands() override {
    std::vector<MonitorCommand> commands;
    for (int index = 0; index < instance_.camera_configs().size(); index++) {
      Command cmd = NewCommand(instance_.camera_socket_path(index));
      Command cmd_log_tee = CF_EXPECT(
          log_tee_.CreateLogTee(cmd, "vhu_media_simple_device", kStdErr), "Failed to create log tee command for media device");
      commands.emplace_back(std::move(cmd));
      commands.emplace_back(std::move(cmd_log_tee));
    }
    return commands;
  }

 private:
  // SetupFeature
  std::string Name() const override { return "VhostUserMediaDevices"; }
  std::unordered_set<SetupFeature*> Dependencies() const override { return {}; }
  Result<void> ResultSetup() override { return {}; }

  const CuttlefishConfig::InstanceSpecific& instance_;
  LogTeeCreator& log_tee_;
};

}  // namespace

fruit::Component<fruit::Required<const CuttlefishConfig::InstanceSpecific, LogTeeCreator>>
VhostUserMediaDevicesComponent() {
  return fruit::createComponent()
      .addMultibinding<CommandSource, VhostUserMediaDevices>();
}

}  // namespace cuttlefish

