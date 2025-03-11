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

#include <sstream>

#include <gflags/gflags.h>
#include <glog/logging.h>

#include "common/libs/fs/shared_buf.h"
#include "common/libs/fs/shared_fd.h"
#include "common/libs/utils/subprocess.h"
#include "host/commands/launch/filesystem_explorer.h"
#include "host/libs/config/cuttlefish_config.h"
#include "host/libs/config/fetcher_config.h"

#include "flag_forwarder.h"

/**
 * If stdin is a tty, that means a user is invoking launch_cvd on the command
 * line and wants automatic file detection for assemble_cvd.
 *
 * If stdin is not a tty, that means launch_cvd is being passed a list of files
 * and that list should be forwarded to assemble_cvd.
 *
 * Controllable with a flag for extraordinary scenarios such as running from a
 * daemon which closes its own stdin.
 */
DEFINE_bool(run_file_discovery, true,
            "Whether to run file discovery or get input files from stdin.");
DEFINE_int32(num_instances, 1, "Number of Android guests to launch");

namespace {

std::string kAssemblerBin = vsoc::DefaultHostArtifactsPath("bin/assemble_cvd");
std::string kRunnerBin = vsoc::DefaultHostArtifactsPath("bin/run_cvd");

cvd::Subprocess StartAssembler(cvd::SharedFD assembler_stdin,
                               cvd::SharedFD assembler_stdout,
                               const std::vector<std::string>& argv) {
  cvd::Command assemble_cmd(kAssemblerBin);
  for (const auto& arg : argv) {
    assemble_cmd.AddParameter(arg);
  }
  if (assembler_stdin->IsOpen()) {
    assemble_cmd.RedirectStdIO(cvd::Subprocess::StdIOChannel::kStdIn, assembler_stdin);
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

void WriteFiles(cvd::FetcherConfig fetcher_config, cvd::SharedFD out) {
  std::stringstream output_streambuf;
  for (const auto& file : fetcher_config.get_cvd_files()) {
    output_streambuf << file.first << "\n";
  }
  std::string output_string = output_streambuf.str();
  int written = cvd::WriteAll(out, output_string);
  if (written < 0) {
    LOG(FATAL) << "Could not write file report (" << strerror(out->GetErrno())
               << ")";
  }
}

} // namespace

int main(int argc, char** argv) {
  ::android::base::InitLogging(argv, android::base::StderrLogger);

  FlagForwarder forwarder({kAssemblerBin, kRunnerBin});

  gflags::ParseCommandLineNonHelpFlags(&argc, &argv, false);

  forwarder.UpdateFlagDefaults();

  gflags::HandleCommandLineHelpFlags();

  cvd::SharedFD assembler_stdout, assembler_stdout_capture;
  cvd::SharedFD::Pipe(&assembler_stdout_capture, &assembler_stdout);

  cvd::SharedFD launcher_report, assembler_stdin;
  bool should_generate_report = FLAGS_run_file_discovery;
  if (should_generate_report) {
    cvd::SharedFD::Pipe(&assembler_stdin, &launcher_report);
  }

  // SharedFDs are std::move-d in to avoid dangling references.
  // Removing the std::move will probably make run_cvd hang as its stdin never closes.
  auto assemble_proc = StartAssembler(std::move(assembler_stdin),
                                      std::move(assembler_stdout),
                                      forwarder.ArgvForSubprocess(kAssemblerBin));

  if (should_generate_report) {
    WriteFiles(AvailableFilesReport(), std::move(launcher_report));
  }

  std::string assembler_output;
  if (cvd::ReadAll(assembler_stdout_capture, &assembler_output) < 0) {
    int error_num = errno;
    LOG(ERROR) << "Read error getting output from assemble_cvd: " << strerror(error_num);
    return -1;
  }

  auto assemble_ret = assemble_proc.Wait();
  if (assemble_ret != 0) {
    LOG(ERROR) << "assemble_cvd returned " << assemble_ret;
    return assemble_ret;
  } else {
    LOG(INFO) << "assemble_cvd exited successfully.";
  }

  std::vector<cvd::Subprocess> runners;
  for (int i = 0; i < FLAGS_num_instances; i++) {
    cvd::SharedFD runner_stdin_in, runner_stdin_out;
    cvd::SharedFD::Pipe(&runner_stdin_out, &runner_stdin_in);
    std::string instance_name = std::to_string(i + vsoc::GetInstance());
    setenv("CUTTLEFISH_INSTANCE", instance_name.c_str(), /* overwrite */ 1);

    auto run_proc = StartRunner(std::move(runner_stdin_out),
                                forwarder.ArgvForSubprocess(kRunnerBin));
    runners.push_back(std::move(run_proc));
    if (cvd::WriteAll(runner_stdin_in, assembler_output) < 0) {
      int error_num = errno;
      LOG(ERROR) << "Could not write to run_cvd: " << strerror(error_num);
      return -1;
    }
  }

  bool run_cvd_failure = false;
  for (auto& run_proc : runners) {
    auto run_ret = run_proc.Wait();
    if (run_ret != 0) {
      run_cvd_failure = true;
      LOG(ERROR) << "run_cvd returned " << run_ret;
    } else {
      LOG(INFO) << "run_cvd exited successfully.";
    }
  }
  return run_cvd_failure ? -1 : 0;
}
