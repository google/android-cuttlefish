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

#include <iostream>
#include <sstream>
#include <fstream>

#include <gflags/gflags.h>
#include <android-base/logging.h>

#include "common/libs/fs/shared_buf.h"
#include "common/libs/fs/shared_fd.h"
#include "common/libs/utils/subprocess.h"
#include "host/commands/start/filesystem_explorer.h"
#include "host/commands/start/flag_forwarder.h"
#include "host/libs/config/cuttlefish_config.h"
#include "host/libs/config/host_tools_version.h"
#include "host/libs/config/fetcher_config.h"

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
DEFINE_string(report_anonymous_usage_stats, "", "Report anonymous usage "
            "statistics for metrics collection and analysis.");
DEFINE_int32(base_instance_num,
             cuttlefish::GetInstance(),
             "The instance number of the device created. When `-num_instances N`"
             " is used, N instance numbers are claimed starting at this number.");
DEFINE_string(verbosity, "INFO", "Console logging verbosity. Options are VERBOSE,"
                                 "DEBUG,INFO,WARNING,ERROR");
DEFINE_string(file_verbosity, "DEBUG",
              "Log file logging verbosity. Options are VERBOSE,DEBUG,INFO,"
              "WARNING,ERROR");

namespace {

std::string kAssemblerBin = cuttlefish::HostBinaryPath("assemble_cvd");
std::string kRunnerBin = cuttlefish::HostBinaryPath("run_cvd");

cuttlefish::Subprocess StartAssembler(cuttlefish::SharedFD assembler_stdin,
                               cuttlefish::SharedFD assembler_stdout,
                               const std::vector<std::string>& argv) {
  cuttlefish::Command assemble_cmd(kAssemblerBin);
  for (const auto& arg : argv) {
    assemble_cmd.AddParameter(arg);
  }
  if (assembler_stdin->IsOpen()) {
    assemble_cmd.RedirectStdIO(cuttlefish::Subprocess::StdIOChannel::kStdIn, assembler_stdin);
  }
  assemble_cmd.RedirectStdIO(cuttlefish::Subprocess::StdIOChannel::kStdOut, assembler_stdout);
  return assemble_cmd.Start();
}

cuttlefish::Subprocess StartRunner(cuttlefish::SharedFD runner_stdin,
                            const std::vector<std::string>& argv) {
  cuttlefish::Command run_cmd(kRunnerBin);
  for (const auto& arg : argv) {
    run_cmd.AddParameter(arg);
  }
  run_cmd.RedirectStdIO(cuttlefish::Subprocess::StdIOChannel::kStdIn, runner_stdin);
  return run_cmd.Start();
}

void WriteFiles(cuttlefish::FetcherConfig fetcher_config, cuttlefish::SharedFD out) {
  std::stringstream output_streambuf;
  for (const auto& file : fetcher_config.get_cvd_files()) {
    output_streambuf << file.first << "\n";
  }
  std::string output_string = output_streambuf.str();
  int written = cuttlefish::WriteAll(out, output_string);
  if (written < 0) {
    LOG(FATAL) << "Could not write file report (" << strerror(out->GetErrno())
               << ")";
  }
}

std::string ValidateMetricsConfirmation(std::string use_metrics) {
  if (use_metrics == "") {
    if (cuttlefish::CuttlefishConfig::ConfigExists()) {
      auto config = cuttlefish::CuttlefishConfig::Get();
      if (config) {
        if (config->enable_metrics() == cuttlefish::CuttlefishConfig::kYes) {
          use_metrics = "y";
        } else if (config->enable_metrics() == cuttlefish::CuttlefishConfig::kNo) {
          use_metrics = "n";
        }
      }
    }
  }
  char ch = !use_metrics.empty() ? tolower(use_metrics.at(0)) : -1;
  if (ch != 'n') {
    std::cout << "===================================================================\n";
    std::cout << "NOTICE:\n\n";
    std::cout << "We collect usage statistics in accordance with our\n"
                 "Content Licenses (https://source.android.com/setup/start/licenses),\n"
                 "Contributor License Agreement (https://cla.developers.google.com/),\n"
                 "Privacy Policy (https://policies.google.com/privacy) and\n"
                 "Terms of Service (https://policies.google.com/terms).\n";
    std::cout << "===================================================================\n\n";
    if (use_metrics.empty()) {
      std::cout << "Do you accept anonymous usage statistics reporting (Y/n)?: ";
    }
  }
  for (;;) {
    switch (ch) {
      case 0:
      case '\r':
      case '\n':
      case 'y':
        return "y";
      case 'n':
        return "n";
      default:
        std::cout << "Must accept/reject anonymous usage statistics reporting (Y/n): ";
        FALLTHROUGH_INTENDED;
      case -1:
        std::cin.get(ch);
        // if there's no tty the EOF flag is set, in which case default to 'n'
        if (std::cin.eof()) {
          ch = 'n';
          std::cout << "n\n";  // for consistency with user input
        }
        ch = tolower(ch);
    }
  }
  return "";
}

bool HostToolsUpdated() {
  if (cuttlefish::CuttlefishConfig::ConfigExists()) {
    auto config = cuttlefish::CuttlefishConfig::Get();
    if (config) {
      auto current_tools = cuttlefish::HostToolsCrc();
      auto last_tools = config->host_tools_version();
      return current_tools != last_tools;
    }
  }
  return true;
}

} // namespace

int main(int argc, char** argv) {
  ::android::base::InitLogging(argv, android::base::StderrLogger);

  FlagForwarder forwarder({kAssemblerBin, kRunnerBin});

  gflags::ParseCommandLineNonHelpFlags(&argc, &argv, false);

  forwarder.UpdateFlagDefaults();

  gflags::HandleCommandLineHelpFlags();

  setenv("CF_CONSOLE_SEVERITY", FLAGS_verbosity.c_str(), /* replace */ false);
  setenv("CF_FILE_SEVERITY", FLAGS_file_verbosity.c_str(), /* replace */ false);

  auto use_metrics = FLAGS_report_anonymous_usage_stats;
  FLAGS_report_anonymous_usage_stats = ValidateMetricsConfirmation(use_metrics);

  // TODO(b/159068082) Make decisions based on this value in assemble_cvd
  LOG(INFO) << "Host changed from last run: " << HostToolsUpdated();

  cuttlefish::SharedFD assembler_stdout, assembler_stdout_capture;
  cuttlefish::SharedFD::Pipe(&assembler_stdout_capture, &assembler_stdout);

  cuttlefish::SharedFD launcher_report, assembler_stdin;
  bool should_generate_report = FLAGS_run_file_discovery;
  if (should_generate_report) {
    cuttlefish::SharedFD::Pipe(&assembler_stdin, &launcher_report);
  }

  auto instance_num_str = std::to_string(FLAGS_base_instance_num);
  setenv("CUTTLEFISH_INSTANCE", instance_num_str.c_str(), /* overwrite */ 1);

#if defined(__BIONIC__)
  // These environment variables are needed in case when Bionic is used.
  // b/171754977
  setenv("ANDROID_DATA", cuttlefish::DefaultHostArtifactsPath("").c_str(), /* overwrite */ 0);
  setenv("ANDROID_TZDATA_ROOT", cuttlefish::DefaultHostArtifactsPath("").c_str(), /* overwrite */ 0);
  setenv("ANDROID_ROOT", cuttlefish::DefaultHostArtifactsPath("").c_str(), /* overwrite */ 0);
#endif

  // SharedFDs are std::move-d in to avoid dangling references.
  // Removing the std::move will probably make run_cvd hang as its stdin never closes.
  auto assemble_proc = StartAssembler(std::move(assembler_stdin),
                                      std::move(assembler_stdout),
                                      forwarder.ArgvForSubprocess(kAssemblerBin));

  if (should_generate_report) {
    WriteFiles(AvailableFilesReport(), std::move(launcher_report));
  }

  std::string assembler_output;
  if (cuttlefish::ReadAll(assembler_stdout_capture, &assembler_output) < 0) {
    int error_num = errno;
    LOG(ERROR) << "Read error getting output from assemble_cvd: " << strerror(error_num);
    return -1;
  }

  auto assemble_ret = assemble_proc.Wait();
  if (assemble_ret != 0) {
    LOG(ERROR) << "assemble_cvd returned " << assemble_ret;
    return assemble_ret;
  } else {
    LOG(DEBUG) << "assemble_cvd exited successfully.";
  }

  std::vector<cuttlefish::Subprocess> runners;
  for (int i = 0; i < FLAGS_num_instances; i++) {
    cuttlefish::SharedFD runner_stdin_in, runner_stdin_out;
    cuttlefish::SharedFD::Pipe(&runner_stdin_out, &runner_stdin_in);
    std::string instance_name = std::to_string(i + FLAGS_base_instance_num);
    setenv("CUTTLEFISH_INSTANCE", instance_name.c_str(), /* overwrite */ 1);

    auto run_proc = StartRunner(std::move(runner_stdin_out),
                                forwarder.ArgvForSubprocess(kRunnerBin));
    runners.push_back(std::move(run_proc));
    if (cuttlefish::WriteAll(runner_stdin_in, assembler_output) < 0) {
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
      LOG(DEBUG) << "run_cvd exited successfully.";
    }
  }
  return run_cvd_failure ? -1 : 0;
}
