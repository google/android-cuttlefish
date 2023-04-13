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

#include "common/libs/utils/result.h"
#include "host/commands/run_cvd/reporting.h"
#include "host/libs/config/command_source.h"
#include "host/libs/config/known_paths.h"

namespace cuttlefish {
namespace {

class ConsoleForwarder : public CommandSource, public DiagnosticInformation {
 public:
  INJECT(ConsoleForwarder(const CuttlefishConfig::InstanceSpecific& instance))
      : instance_(instance) {}
  // DiagnosticInformation
  std::vector<std::string> Diagnostics() const override {
    if (Enabled()) {
      return {"To access the console run: screen " + instance_.console_path()};
    } else {
      return {"Serial console is disabled; use -console=true to enable it."};
    }
  }

  // CommandSource
  Result<std::vector<MonitorCommand>> Commands() override {
    Command console_forwarder_cmd(ConsoleForwarderBinary());
    console_forwarder_cmd.AddParameter("--console_in_fd=",
                                       console_forwarder_in_wr_);
    console_forwarder_cmd.AddParameter("--console_out_fd=",
                                       console_forwarder_out_rd_);
    std::vector<MonitorCommand> commands;
    commands.emplace_back(std::move(console_forwarder_cmd));
    return commands;
  }

  // SetupFeature
  std::string Name() const override { return "ConsoleForwarder"; }
  bool Enabled() const override { return instance_.console(); }

 private:
  std::unordered_set<SetupFeature*> Dependencies() const override { return {}; }
  Result<void> ResultSetup() override {
    auto console_in_pipe_name = instance_.console_in_pipe_name();
    CF_EXPECT(
        mkfifo(console_in_pipe_name.c_str(), 0600) == 0,
        "Failed to create console input fifo for crosvm: " << strerror(errno));

    auto console_out_pipe_name = instance_.console_out_pipe_name();
    CF_EXPECT(
        mkfifo(console_out_pipe_name.c_str(), 0660) == 0,
        "Failed to create console output fifo for crosvm: " << strerror(errno));

    // These fds will only be read from or written to, but open them with
    // read and write access to keep them open in case the subprocesses exit
    console_forwarder_in_wr_ =
        SharedFD::Open(console_in_pipe_name.c_str(), O_RDWR);
    CF_EXPECT(console_forwarder_in_wr_->IsOpen(),
              "Failed to open console_forwarder input fifo for writes: "
                  << console_forwarder_in_wr_->StrError());

    console_forwarder_out_rd_ =
        SharedFD::Open(console_out_pipe_name.c_str(), O_RDWR);
    CF_EXPECT(console_forwarder_out_rd_->IsOpen(),
              "Failed to open console_forwarder output fifo for reads: "
                  << console_forwarder_out_rd_->StrError());
    return {};
  }

  const CuttlefishConfig::InstanceSpecific& instance_;
  SharedFD console_forwarder_in_wr_;
  SharedFD console_forwarder_out_rd_;
};

}  // namespace

fruit::Component<fruit::Required<const CuttlefishConfig::InstanceSpecific>>
ConsoleForwarderComponent() {
  return fruit::createComponent()
      .addMultibinding<CommandSource, ConsoleForwarder>()
      .addMultibinding<SetupFeature, ConsoleForwarder>();
}

}  // namespace cuttlefish
