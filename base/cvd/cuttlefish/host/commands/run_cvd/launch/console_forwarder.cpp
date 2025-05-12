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

#include "cuttlefish/host/commands/run_cvd/launch/console_forwarder.h"

#include <optional>
#include <string>

#include "cuttlefish/common/libs/fs/shared_fd.h"
#include "cuttlefish/common/libs/utils/result.h"
#include "cuttlefish/common/libs/utils/subprocess.h"
#include "cuttlefish/host/libs/config/cuttlefish_config.h"
#include "cuttlefish/host/libs/config/known_paths.h"
#include "cuttlefish/host/libs/feature/command_source.h"

namespace cuttlefish {

std::string ConsoleInfo(const CuttlefishConfig::InstanceSpecific& instance) {
  if (instance.console()) {
    return "To access the console run: screen " + instance.console_path();
  } else {
    return "Serial console is disabled; use -console=true to enable it.";
  }
}

Result<std::optional<MonitorCommand>> ConsoleForwarder(
    const CuttlefishConfig::InstanceSpecific& instance) {
  if (!instance.console()) {
    return {};
  }
  // These fds will only be read from or written to, but open them with
  // read and write access to keep them open in case the subprocesses exit
  auto console_in_pipe_name = instance.console_in_pipe_name();
  auto console_forwarder_in_wr =
      CF_EXPECT(SharedFD::Fifo(console_in_pipe_name, 0600));

  auto console_out_pipe_name = instance.console_out_pipe_name();
  auto console_forwarder_out_rd =
      CF_EXPECT(SharedFD::Fifo(console_out_pipe_name, 0600));

  Command console_forwarder_cmd(ConsoleForwarderBinary());
  return Command(ConsoleForwarderBinary())
      .AddParameter("--console_in_fd=", console_forwarder_in_wr)
      .AddParameter("--console_out_fd=", console_forwarder_out_rd);
}

}  // namespace cuttlefish
