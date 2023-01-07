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

#include <fstream>
#include <iostream>
#include <sstream>
#include <unordered_set>

#include <android-base/logging.h>
#include <android-base/parseint.h>
#include <gflags/gflags.h>

#include "common/libs/fs/shared_buf.h"
#include "common/libs/fs/shared_fd.h"
#include "common/libs/utils/subprocess.h"
#include "host/commands/assemble_cvd/flags_defaults.h"
#include "host/commands/start/filesystem_explorer.h"
#include "host/commands/start/flag_forwarder.h"
#include "host/libs/config/cuttlefish_config.h"
#include "host/libs/config/fetcher_config.h"
#include "host/libs/config/host_tools_version.h"
#include "host/libs/config/instance_nums.h"
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
DEFINE_bool(run_file_discovery, CF_DEFAULTS_RUN_FILE_DISCOVERY,
            "Whether to run file discovery or get input files from stdin.");
DEFINE_int32(num_instances, CF_DEFAULTS_NUM_INSTANCES,
             "Number of Android guests to launch");
DEFINE_string(report_anonymous_usage_stats,
              CF_DEFAULTS_REPORT_ANONYMOUS_USAGE_STATS,
              "Report anonymous usage "
              "statistics for metrics collection and analysis.");
DEFINE_int32(
    base_instance_num, CF_DEFAULTS_BASE_INSTANCE_NUM,
    "The instance number of the device created. When `-num_instances N`"
    " is used, N instance numbers are claimed starting at this number.");
DEFINE_string(instance_nums, CF_DEFAULTS_INSTANCE_NUMS,
              "A comma-separated list of instance numbers "
              "to use. Mutually exclusive with base_instance_num.");
DEFINE_string(verbosity, CF_DEFAULTS_VERBOSITY,
              "Console logging verbosity. Options are VERBOSE,"
              "DEBUG,INFO,WARNING,ERROR");
DEFINE_string(file_verbosity, CF_DEFAULTS_FILE_VERBOSITY,
              "Log file logging verbosity. Options are VERBOSE,DEBUG,INFO,"
              "WARNING,ERROR");
DEFINE_bool(use_overlay, CF_DEFAULTS_USE_OVERLAY,
            "Capture disk writes an overlay. This is a "
            "prerequisite for powerwash_cvd or multiple instances.");

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

  std::cout << "===================================================================\n";
  std::cout << "NOTICE:\n\n";
  std::cout << "By using this Android Virtual Device, you agree to\n";
  std::cout << "Google Terms of Service (https://policies.google.com/terms).\n";
  std::cout << "The Google Privacy Policy (https://policies.google.com/privacy)\n";
  std::cout << "describes how Google handles information generated as you use\n";
  std::cout << "Google Services.";
  char ch = !use_metrics.empty() ? tolower(use_metrics.at(0)) : -1;
  if (ch != 'n') {
    if (use_metrics.empty()) {
      std::cout << "\n===================================================================\n";
      std::cout << "Automatically send diagnostic information to Google, such as crash\n";
      std::cout << "reports and usage data from this Android Virtual Device. You can\n";
      std::cout << "adjust this permission at any time by running\n";
      std::cout << "\"launch_cvd -report_anonymous_usage_stats=n\". (Y/n)?:";
    } else {
      std::cout << " You can adjust the permission for sending\n";
      std::cout << "diagnostic information to Google, such as crash reports and usage\n";
      std::cout << "data from this Android Virtual Device, at any time by running\n";
      std::cout << "\"launch_cvd -report_anonymous_usage_stats=n\"\n";
      std::cout << "===================================================================\n\n";
    }
  } else {
    std::cout << "\n===================================================================\n\n";
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

// Hash table for all bool flag names
// Used to find bool flag and convert "flag"/"noflag" to "--flag=value"
// This is the solution for vectorize bool flags in gFlags

std::unordered_set<std::string> kBoolFlags = {"guest_enforce_security",
                                              "use_random_serial",
                                              "use_allocd",
                                              "use_sdcard",
                                              "pause_in_bootloader",
                                              "daemon",
                                              "enable_minimal_mode",
                                              "enable_modem_simulator",
                                              "console",
                                              "enable_sandbox",
                                              "restart_subprocesses",
                                              "enable_gpu_udmabuf",
                                              "enable_gpu_angle",
                                              "enable_audio",
                                              "enable_vehicle_hal_grpc_server",
                                              "start_gnss_proxy",
                                              "enable_bootanimation",
                                              "record_screen",
                                              "protected_vm",
                                              "enable_kernel_log",
                                              "kgdb",
                                              "start_webrtc",
                                              "smt",
                                              "vhost_net"};

struct BooleanFlag {
  bool is_bool_flag;
  bool bool_flag_value;
  std::string name;
};
BooleanFlag IsBoolArg(const std::string& argument) {
  // Validate format
  // we only deal with special bool case: -flag, --flag, -noflag, --noflag
  // and convert to -flag=true, --flag=true, -flag=false, --flag=false
  // others not in this format just return false
  std::string_view name = argument;
  if (!android::base::ConsumePrefix(&name, "-")) {
    return {false, false, ""};
  }
  android::base::ConsumePrefix(&name, "-");
  std::size_t found = name.find('=');
  if (found != std::string::npos) {
    // found "=", --flag=value case, it doesn't need convert
    return {false, false, ""};
  }

  // Validate it is part of the set
  std::string result_name(name);
  std::string_view new_name = result_name;
  if (result_name.length() == 0) {
    return {false, false, ""};
  }
  if (kBoolFlags.find(result_name) != kBoolFlags.end()) {
    // matched -flag, --flag
    return {true, true, result_name};
  } else if (android::base::ConsumePrefix(&new_name, "no")) {
    // 2nd chance to check -noflag, --noflag
    result_name = new_name;
    if (kBoolFlags.find(result_name) != kBoolFlags.end()) {
      // matched -noflag, --noflag
      return {true, false, result_name};
    }
  }
  // return status
  return {false, false, ""};
}

std::string FormatBoolString(const std::string& name_str, bool value) {
  std::string new_flag = "--" + name_str;
  if (value) {
    new_flag += "=true";
  } else {
    new_flag += "=false";
  }
  return new_flag;
}

bool OverrideBoolArg(std::vector<std::string>& args) {
  bool overrided = false;
  for (int index = 0; index < args.size(); index++) {
    const std::string curr_arg = args[index];
    BooleanFlag value = IsBoolArg(curr_arg);
    if (value.is_bool_flag) {
      // Override the value
      args[index] = FormatBoolString(value.name, value.bool_flag_value);
      overrided = true;
    }
  }
  return overrided;
}

} // namespace

int main(int argc, char** argv) {
  ::android::base::InitLogging(argv, android::base::StderrLogger);

  FlagForwarder forwarder({kAssemblerBin, kRunnerBin});

  // Used to find bool flag and convert "flag"/"noflag" to "--flag=value"
  // This is the solution for vectorize bool flags in gFlags
  std::vector<std::string> args(argv + 1, argv + argc);
  if (OverrideBoolArg(args)) {
    for (int i = 1; i < argc; i++) {
      argv[i] = &args[i-1][0]; // args[] start from 0
    }
  }

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

  auto instance_nums =
      cuttlefish::InstanceNumsCalculator().FromGlobalGflags().Calculate();
  if (!instance_nums.ok()) {
    LOG(ERROR) << instance_nums.error().Message();
    LOG(DEBUG) << instance_nums.error().Trace();
    abort();
  }

  if (cuttlefish::CuttlefishConfig::ConfigExists()) {
    auto previous_config = cuttlefish::CuttlefishConfig::Get();
    CHECK(previous_config);
    CHECK(previous_config->Instances().size() > 0);
    auto previous_instance = previous_config->Instances()[0];
    const auto& disks = previous_instance.virtual_disk_paths();
    auto overlay = previous_instance.PerInstancePath("overlay.img");
    auto used_overlay =
        std::find(disks.begin(), disks.end(), overlay) != disks.end();
    CHECK(used_overlay == FLAGS_use_overlay)
        << "Cannot transition between different values of --use_overlay "
        << "(Previous = " << used_overlay << ", current = " << FLAGS_use_overlay
        << "). To fix this, delete \"" << previous_config->root_dir()
        << "\" and any image files.";
  }

  CHECK(instance_nums->size() > 0) << "Expected at least one instance";
  auto instance_num_str = std::to_string(*instance_nums->begin());
  setenv(cuttlefish::kCuttlefishInstanceEnvVarName, instance_num_str.c_str(),
         /* overwrite */ 1);

#if defined(__BIONIC__)
  // These environment variables are needed in case when Bionic is used.
  // b/171754977
  setenv("ANDROID_DATA", cuttlefish::DefaultHostArtifactsPath("").c_str(), /* overwrite */ 0);
  setenv("ANDROID_TZDATA_ROOT", cuttlefish::DefaultHostArtifactsPath("").c_str(), /* overwrite */ 0);
  setenv("ANDROID_ROOT", cuttlefish::DefaultHostArtifactsPath("").c_str(), /* overwrite */ 0);
#endif

  // SharedFDs are std::move-d in to avoid dangling references.
  // Removing the std::move will probably make run_cvd hang as its stdin never closes.
  auto assemble_proc =
      StartAssembler(std::move(assembler_stdin), std::move(assembler_stdout),
                     forwarder.ArgvForSubprocess(kAssemblerBin, args));

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
  for (const auto& instance_num : *instance_nums) {
    cuttlefish::SharedFD runner_stdin_in, runner_stdin_out;
    cuttlefish::SharedFD::Pipe(&runner_stdin_out, &runner_stdin_in);
    std::string instance_num_str = std::to_string(instance_num);
    setenv(cuttlefish::kCuttlefishInstanceEnvVarName, instance_num_str.c_str(),
           /* overwrite */ 1);

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
