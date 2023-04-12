//
// Copyright (C) 2019 The Android Open Source Project
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

#include "host/commands/run_cvd/launch/launch.h"

#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

#include <fruit/fruit.h>

#include "common/libs/utils/files.h"
#include "common/libs/utils/result.h"
#include "host/libs/config/command_source.h"
#include "host/libs/config/known_paths.h"

namespace cuttlefish {

class TombstoneReceiver : public CommandSource {
 public:
  INJECT(TombstoneReceiver(const CuttlefishConfig::InstanceSpecific& instance))
      : instance_(instance) {}

  // CommandSource
  Result<std::vector<MonitorCommand>> Commands() override {
    Command command(TombstoneReceiverBinary());
    command.AddParameter("-server_fd=", socket_);
    command.AddParameter("-tombstone_dir=", tombstone_dir_);
    std::vector<MonitorCommand> commands;
    commands.emplace_back(std::move(command));
    return commands;
  }

  // SetupFeature
  std::string Name() const override { return "TombstoneReceiver"; }
  bool Enabled() const override { return true; }

 private:
  std::unordered_set<SetupFeature*> Dependencies() const override { return {}; }
  Result<void> ResultSetup() override {
    tombstone_dir_ = instance_.PerInstancePath("tombstones");
    if (!DirectoryExists(tombstone_dir_)) {
      LOG(DEBUG) << "Setting up " << tombstone_dir_;
      CF_EXPECT(mkdir(tombstone_dir_.c_str(),
                      S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH) == 0,
                "Failed to create tombstone directory: "
                    << tombstone_dir_ << ". Error: " << strerror(errno));
    }

    auto port = instance_.tombstone_receiver_port();
    socket_ = SharedFD::VsockServer(port, SOCK_STREAM);
    CF_EXPECT(socket_->IsOpen(), "Unable to create tombstone server socket: "
                                     << socket_->StrError());
    return {};
  }

  const CuttlefishConfig::InstanceSpecific& instance_;
  SharedFD socket_;
  std::string tombstone_dir_;
};

fruit::Component<fruit::Required<const CuttlefishConfig::InstanceSpecific>>
TombstoneReceiverComponent() {
  return fruit::createComponent()
      .addMultibinding<CommandSource, TombstoneReceiver>()
      .addMultibinding<SetupFeature, TombstoneReceiver>();
}

}  // namespace cuttlefish
