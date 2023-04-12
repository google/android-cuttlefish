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
#include "host/libs/config/inject.h"
#include "host/libs/config/known_paths.h"

namespace cuttlefish {
namespace {

class KernelLogMonitor : public CommandSource,
                         public KernelLogPipeProvider,
                         public DiagnosticInformation,
                         public LateInjected {
 public:
  INJECT(KernelLogMonitor(const CuttlefishConfig::InstanceSpecific& instance))
      : instance_(instance) {}

  // DiagnosticInformation
  std::vector<std::string> Diagnostics() const override {
    return {"Kernel log: " + instance_.PerInstancePath("kernel.log")};
  }

  Result<void> LateInject(fruit::Injector<>& injector) override {
    number_of_event_pipes_ =
        injector.getMultibindings<KernelLogPipeConsumer>().size();
    return {};
  }

  // CommandSource
  Result<std::vector<MonitorCommand>> Commands() override {
    Command command(KernelLogMonitorBinary());
    command.AddParameter("-log_pipe_fd=", fifo_);

    if (!event_pipe_write_ends_.empty()) {
      command.AddParameter("-subscriber_fds=");
      for (size_t i = 0; i < event_pipe_write_ends_.size(); i++) {
        if (i > 0) {
          command.AppendToLastParameter(",");
        }
        command.AppendToLastParameter(event_pipe_write_ends_[i]);
      }
    }
    std::vector<MonitorCommand> commands;
    commands.emplace_back(std::move(command));
    return commands;
  }

  // KernelLogPipeProvider
  SharedFD KernelLogPipe() override {
    CHECK(!event_pipe_read_ends_.empty()) << "No more kernel pipes left. Make sure you inhereted "
                                             "KernelLogPipeProvider and provided multibinding "
                                             "from KernelLogPipeConsumer to your type.";
    SharedFD ret = event_pipe_read_ends_.back();
    event_pipe_read_ends_.pop_back();
    return ret;
  }

 private:
  // SetupFeature
  bool Enabled() const override { return true; }
  std::string Name() const override { return "KernelLogMonitor"; }

 private:
  std::unordered_set<SetupFeature*> Dependencies() const override { return {}; }
  Result<void> ResultSetup() override {
    auto log_name = instance_.kernel_log_pipe_name();
    CF_EXPECT(mkfifo(log_name.c_str(), 0600) == 0,
              "Unable to create named pipe at " << log_name << ": "
                                                << strerror(errno));

    // Open the pipe here (from the launcher) to ensure the pipe is not deleted
    // due to the usage counters in the kernel reaching zero. If this is not
    // done and the kernel_log_monitor crashes for some reason the VMM may get
    // SIGPIPE.
    fifo_ = SharedFD::Open(log_name, O_RDWR);
    CF_EXPECT(fifo_->IsOpen(),
              "Unable to open \"" << log_name << "\": " << fifo_->StrError());

    for (unsigned int i = 0; i < number_of_event_pipes_; ++i) {
      SharedFD event_pipe_write_end, event_pipe_read_end;
      CF_EXPECT(SharedFD::Pipe(&event_pipe_read_end, &event_pipe_write_end),
                "Failed creating kernel log pipe: " << strerror(errno));
      event_pipe_write_ends_.push_back(event_pipe_write_end);
      event_pipe_read_ends_.push_back(event_pipe_read_end);
    }
    return {};
  }

  int number_of_event_pipes_ = 0;
  const CuttlefishConfig::InstanceSpecific& instance_;
  SharedFD fifo_;
  std::vector<SharedFD> event_pipe_write_ends_;
  std::vector<SharedFD> event_pipe_read_ends_;
};

}  // namespace

fruit::Component<fruit::Required<const CuttlefishConfig::InstanceSpecific>,
                 KernelLogPipeProvider>
KernelLogMonitorComponent() {
  return fruit::createComponent()
      .bind<KernelLogPipeProvider, KernelLogMonitor>()
      .addMultibinding<CommandSource, KernelLogMonitor>()
      .addMultibinding<SetupFeature, KernelLogMonitor>()
      .addMultibinding<DiagnosticInformation, KernelLogMonitor>()
      .addMultibinding<LateInjected, KernelLogMonitor>();
}

}  // namespace cuttlefish
