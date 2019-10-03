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

#include <gflags/gflags.h>
#include <glog/logging.h>

#include "common/libs/fs/shared_fd.h"
#include "common/libs/utils/subprocess.h"
#include "host/libs/config/cuttlefish_config.h"

#include "flag_forwarder.h"

namespace {

std::string kAssemblerBin = vsoc::DefaultHostArtifactsPath("bin/assemble_cvd");
std::string kRunnerBin = vsoc::DefaultHostArtifactsPath("bin/run_cvd");

cvd::Subprocess StartAssembler(cvd::SharedFD assembler_stdout,
                               const std::vector<std::string>& argv) {
  cvd::Command assemble_cmd(kAssemblerBin);
  for (const auto& arg : argv) {
    assemble_cmd.AddParameter(arg);
  }
  assemble_cmd.RedirectStdIO(cvd::Subprocess::StdIOChannel::kStdOut, assembler_stdout);
  return assemble_cmd.Start();
}

cvd::Subprocess StartRunner(cvd::SharedFD runner_stdin,
                            const std::vector<std::string>& argv) {
  cvd::Command run_cmd(kRunnerBin);
  for (const auto& arg : argv) {
    run_cmd.AddParameter(arg);
  }
  run_cmd.RedirectStdIO(cvd::Subprocess::StdIOChannel::kStdIn, runner_stdin);
  return run_cmd.Start();
}

} // namespace

int main(int argc, char** argv) {
  ::android::base::InitLogging(argv, android::base::StderrLogger);

  FlagForwarder forwarder({kAssemblerBin, kRunnerBin});

  gflags::ParseCommandLineNonHelpFlags(&argc, &argv, false);

  forwarder.UpdateFlagDefaults();

  gflags::HandleCommandLineHelpFlags();

  cvd::SharedFD assembler_stdout, runner_stdin;
  cvd::SharedFD::Pipe(&runner_stdin, &assembler_stdout);

  // SharedFDs are std::move-d in to avoid dangling references.
  // Removing the std::move will probably make run_cvd hang as its stdin never closes.
  auto assemble_proc = StartAssembler(std::move(assembler_stdout),
                                      forwarder.ArgvForSubprocess(kAssemblerBin));
  auto run_proc = StartRunner(std::move(runner_stdin),
                              forwarder.ArgvForSubprocess(kRunnerBin));

  auto assemble_ret = assemble_proc.Wait();
  if (assemble_ret != 0) {
    LOG(ERROR) << "assemble_cvd returned " << assemble_ret;
    return assemble_ret;
  } else {
    LOG(INFO) << "assemble_cvd exited successfully.";
  }

  auto run_ret = run_proc.Wait();
  if (run_ret != 0) {
    LOG(ERROR) << "run_cvd returned " << run_ret;
  } else {
    LOG(INFO) << "run_cvd exited successfully.";
  }
  return run_ret;
}
