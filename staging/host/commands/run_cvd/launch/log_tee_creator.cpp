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

namespace cuttlefish {

LogTeeCreator::LogTeeCreator(const CuttlefishConfig::InstanceSpecific& instance)
    : instance_(instance) {}

Command LogTeeCreator::CreateLogTee(Command& cmd,
                                    const std::string& process_name) {
  auto name_with_ext = process_name + "_logs.fifo";
  auto logs_path = instance_.PerInstanceInternalPath(name_with_ext.c_str());
  auto logs = SharedFD::Fifo(logs_path, 0666);
  if (!logs->IsOpen()) {
    LOG(FATAL) << "Failed to create fifo for " << process_name
               << " output: " << logs->StrError();
  }

  cmd.RedirectStdIO(Subprocess::StdIOChannel::kStdOut, logs);
  cmd.RedirectStdIO(Subprocess::StdIOChannel::kStdErr, logs);

  return Command(HostBinaryPath("log_tee"))
      .AddParameter("--process_name=", process_name)
      .AddParameter("--log_fd_in=", logs);
}

}  // namespace cuttlefish
