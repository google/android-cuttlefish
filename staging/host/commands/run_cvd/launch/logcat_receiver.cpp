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

class LogcatReceiver : public CommandSource, public DiagnosticInformation {
 public:
  INJECT(LogcatReceiver(const CuttlefishConfig::InstanceSpecific& instance))
      : instance_(instance) {}
  // DiagnosticInformation
  std::vector<std::string> Diagnostics() const override {
    return {"Logcat output: " + instance_.logcat_path()};
  }

  // CommandSource
  Result<std::vector<MonitorCommand>> Commands() override {
    Command command(LogcatReceiverBinary());
    command.AddParameter("-log_pipe_fd=", pipe_);
    std::vector<MonitorCommand> commands;
    commands.emplace_back(std::move(command));
    return commands;
  }

  // SetupFeature
  std::string Name() const override { return "LogcatReceiver"; }
  bool Enabled() const override { return true; }

 private:
  std::unordered_set<SetupFeature*> Dependencies() const override { return {}; }
  Result<void> ResultSetup() {
    auto log_name = instance_.logcat_pipe_name();
    CF_EXPECT(mkfifo(log_name.c_str(), 0600) == 0,
              "Unable to create named pipe at " << log_name << ": "
                                                << strerror(errno));
    // Open the pipe here (from the launcher) to ensure the pipe is not deleted
    // due to the usage counters in the kernel reaching zero. If this is not
    // done and the logcat_receiver crashes for some reason the VMM may get
    // SIGPIPE.
    pipe_ = SharedFD::Open(log_name.c_str(), O_RDWR);
    CF_EXPECT(pipe_->IsOpen(),
              "Can't open \"" << log_name << "\": " << pipe_->StrError());
    return {};
  }

  const CuttlefishConfig::InstanceSpecific& instance_;
  SharedFD pipe_;
};

}  // namespace

fruit::Component<fruit::Required<const CuttlefishConfig::InstanceSpecific>>
LogcatReceiverComponent() {
  return fruit::createComponent()
      .addMultibinding<CommandSource, LogcatReceiver>()
      .addMultibinding<SetupFeature, LogcatReceiver>()
      .addMultibinding<DiagnosticInformation, LogcatReceiver>();
}

}  // namespace cuttlefish
