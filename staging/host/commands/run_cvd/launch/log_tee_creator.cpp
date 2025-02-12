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

#include "host/commands/run_cvd/launch/log_tee_creator.h"

#include <vector>

#include "common/libs/utils/result.h"
#include "common/libs/utils/subprocess.h"
#include "host/libs/config/cuttlefish_config.h"

namespace cuttlefish {

namespace {
Result<Command> CreateLogTeeImpl(
    Command& cmd, const CuttlefishConfig::InstanceSpecific& instance,
    std::string process_name,
    const std::vector<Subprocess::StdIOChannel>& log_channels) {
  auto name_with_ext = process_name + "_logs.fifo";
  auto logs_path = instance.PerInstanceInternalPath(name_with_ext.c_str());
  auto logs = CF_EXPECT(SharedFD::Fifo(logs_path, 0666));

  for (const auto& channel : log_channels) {
    cmd.RedirectStdIO(channel, logs);
  }

  return Command(HostBinaryPath("log_tee"))
      .AddParameter("--process_name=", process_name)
      .AddParameter("--log_fd_in=", logs);
}
}  // namespace

LogTeeCreator::LogTeeCreator(const CuttlefishConfig::InstanceSpecific& instance)
    : instance_(instance) {}

Result<Command> LogTeeCreator::CreateFullLogTee(Command& cmd,
                                                std::string process_name) {
  return CF_EXPECT(CreateLogTeeImpl(
      cmd, instance_, std::move(process_name),
      {Subprocess::StdIOChannel::kStdOut, Subprocess::StdIOChannel::kStdErr}));
}

Result<Command> LogTeeCreator::CreateLogTee(
    Command& cmd, std::string process_name,
    Subprocess::StdIOChannel log_channel) {
  CF_EXPECT(log_channel != Subprocess::StdIOChannel::kStdIn,
            "Invalid channel for log tee: stdin");
  return CF_EXPECT(
      CreateLogTeeImpl(cmd, instance_, std::move(process_name), {log_channel}));
}

}  // namespace cuttlefish
