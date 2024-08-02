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

#include <android-base/file.h>
#include <android-base/logging.h>
#include <android-base/no_destructor.h>
#include <android-base/parseint.h>
#include <gflags/gflags.h>

#include "common/libs/fs/shared_buf.h"
#include "common/libs/fs/shared_fd.h"
#include "common/libs/utils/files.h"
#include "common/libs/utils/flag_parser.h"
#include "common/libs/utils/subprocess.h"
#include "host/commands/assemble_cvd/flags_defaults.h"
#include "host/commands/start/filesystem_explorer.h"
#include "host/commands/start/flag_forwarder.h"
#include "host/commands/start/override_bool_arg.h"
#include "host/libs/config/config_utils.h"
#include "host/libs/config/cuttlefish_config.h"
#include "host/libs/config/fetcher_config.h"
#include "host/libs/config/host_tools_version.h"
#include "host/libs/config/instance_nums.h"

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
DEFINE_bool(track_host_tools_crc, CF_DEFAULTS_TRACK_HOST_TOOLS_CRC,
            "Track changes to host executables");
DEFINE_bool(enable_host_sandbox, CF_DEFAULTS_HOST_SANDBOX,
            "Lock down host processes with sandbox2");

namespace cuttlefish {
namespace {

using android::base::NoDestructor;

std::string SubtoolPath(const std::string& subtool_base) {
  auto my_own_dir = android::base::GetExecutableDirectory();
  std::stringstream subtool_path_stream;
  subtool_path_stream << my_own_dir << "/" << subtool_base;
  auto subtool_path = subtool_path_stream.str();
  if (my_own_dir.empty() || !FileExists(subtool_path)) {
    return HostBinaryPath(subtool_base);
  }
  return subtool_path;
}

std::string AssemblerPath() { return SubtoolPath("assemble_cvd"); }
std::string RunnerPath() { return SubtoolPath("run_cvd"); }
std::string SandboxerPath() { return SubtoolPath("process_sandboxer"); }

int InvokeAssembler(const std::string& assembler_stdin,
                    std::string& assembler_stdout,
                    const std::vector<std::string>& argv) {
  Command assemble_cmd(AssemblerPath());
  for (const auto& arg : argv) {
    assemble_cmd.AddParameter(arg);
  }
  return RunWithManagedStdio(std::move(assemble_cmd), &assembler_stdin,
                             &assembler_stdout, nullptr);
}

Subprocess StartRunner(SharedFD runner_stdin, const CuttlefishConfig& config,
                       const CuttlefishConfig::InstanceSpecific& instance,
                       const std::vector<std::string>& argv) {
  Command run_cmd(FLAGS_enable_host_sandbox ? SandboxerPath() : RunnerPath());
  if (FLAGS_enable_host_sandbox) {
    run_cmd.AddParameter("--environments_dir=", config.environments_dir())
        .AddParameter("--environments_uds_dir=", config.environments_uds_dir())
        .AddParameter("--instance_uds_dir=", instance.instance_uds_dir())
        .AddParameter("--log_dir=", instance.PerInstanceLogPath(""))
        .AddParameter("--runtime_dir=", instance.instance_dir())
        .AddParameter("--host_artifacts_path=", DefaultHostArtifactsPath(""));
    std::string log_files = instance.PerInstanceLogPath("sandbox.log");
    if (!instance.run_as_daemon()) {
      log_files += "," + instance.PerInstanceLogPath("launcher.log");
    }
    run_cmd.AddParameter("--log_files=", log_files);
    run_cmd.AddParameter("--").AddParameter(RunnerPath());
  }
  // Note: Do not pass any SharedFD arguments, they will not work as expected in
  // sandbox mode.
  for (const auto& arg : argv) {
    run_cmd.AddParameter(arg);
  }
  run_cmd.RedirectStdIO(Subprocess::StdIOChannel::kStdIn, runner_stdin);
  run_cmd.SetWorkingDirectory(instance.instance_dir());
  return run_cmd.Start();
}

std::string WriteFiles(FetcherConfig fetcher_config) {
  std::stringstream output_streambuf;
  for (const auto& file : fetcher_config.get_cvd_files()) {
    output_streambuf << file.first << "\n";
  }
  return output_streambuf.str();
}

std::string ValidateMetricsConfirmation(std::string use_metrics) {
  if (use_metrics == "") {
    if (CuttlefishConfig::ConfigExists()) {
      auto config = CuttlefishConfig::Get();
      if (config) {
        if (config->enable_metrics() == CuttlefishConfig::Answer::kYes) {
          use_metrics = "y";
        } else if (config->enable_metrics() == CuttlefishConfig::Answer::kNo) {
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
  if (CuttlefishConfig::ConfigExists()) {
    auto config = CuttlefishConfig::Get();
    if (config) {
      auto current_tools = HostToolsCrc();
      auto last_tools = config->host_tools_version();
      return current_tools != last_tools;
    }
  }
  return true;
}

// Hash table for all bool flag names
// Used to find bool flag and convert "flag"/"noflag" to "--flag=value"
// This is the solution for vectorize bool flags in gFlags

const std::unordered_set<std::string>& BoolFlags() {
  static const NoDestructor<std::unordered_set<std::string>> bool_flags({
      "chromeos_boot",
      "console",
      "daemon",
      "enable_audio",
      "enable_bootanimation",
      "enable_gpu_udmabuf",
      "enable_gpu_vhost_user",
      "enable_host_sandbox",
      "enable_kernel_log",
      "enable_minimal_mode",
      "enable_modem_simulator",
      "enable_sandbox",
      "enable_usb",
      "enable_virtiofs",
      "fail_fast",
      "guest_enforce_security",
      "kgdb",
      "pause_in_bootloader",
      "protected_vm",
      "record_screen",
      "restart_subprocesses",
      "smt",
      "start_gnss_proxy",
      "start_webrtc",
      "use_allocd",
      "use_random_serial",
      "use_sdcard",
      "vhost_net",
      "vhost_user_block",
      "vhost_user_vsock",
  });
  return *bool_flags;
}

int CvdInternalStartMain(int argc, char** argv) {
  ::android::base::InitLogging(argv, android::base::StderrLogger);

  std::vector<std::string> args(argv + 1, argv + argc);

  std::vector<std::string> assemble_args;
  std::string image_dir;
  std::vector<std::string> args_copy = args;
  auto parse_res = ConsumeFlags(
      {GflagsCompatFlag("system_image_dir", image_dir)}, args_copy);
  LOG(INFO) << "Using system_image_dir of: " << image_dir;

  if (!parse_res.ok()) {
    LOG(ERROR) << "Error extracting system_image_dir from args: "
               << parse_res.error().FormatForEnv();
    return -1;
  } else if (!image_dir.empty()) {
    assemble_args = {"--system_image_dir=" + image_dir};
  }

  std::vector<std::vector<std::string>> spargs = {assemble_args, {}};
  FlagForwarder forwarder({AssemblerPath(), RunnerPath()}, spargs);

  // Used to find bool flag and convert "flag"/"noflag" to "--flag=value"
  // This is the solution for vectorize bool flags in gFlags
  args = OverrideBoolArg(std::move(args), BoolFlags());
  for (int i = 1; i < argc; i++) {
    argv[i] = args[i - 1].data();  // args[] start from 0
  }

  gflags::ParseCommandLineNonHelpFlags(&argc, &argv, false);

  forwarder.UpdateFlagDefaults();

  gflags::HandleCommandLineHelpFlags();

  setenv("CF_CONSOLE_SEVERITY", FLAGS_verbosity.c_str(), /* replace */ false);
  setenv("CF_FILE_SEVERITY", FLAGS_file_verbosity.c_str(), /* replace */ false);

  auto use_metrics = FLAGS_report_anonymous_usage_stats;
  FLAGS_report_anonymous_usage_stats = ValidateMetricsConfirmation(use_metrics);

  if (FLAGS_track_host_tools_crc) {
    // TODO(b/159068082) Make decisions based on this value in assemble_cvd
    LOG(INFO) << "Host changed from last run: " << HostToolsUpdated();
  }

  auto instance_nums = InstanceNumsCalculator().FromGlobalGflags().Calculate();
  if (!instance_nums.ok()) {
    LOG(ERROR) << instance_nums.error().FormatForEnv();
    abort();
  }

  if (CuttlefishConfig::ConfigExists()) {
    auto previous_config = CuttlefishConfig::Get();
    CHECK(previous_config);
    CHECK(!previous_config->Instances().empty());
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

  CHECK(!instance_nums->empty()) << "Expected at least one instance";
  auto instance_num_str = std::to_string(*instance_nums->begin());
  setenv(kCuttlefishInstanceEnvVarName, instance_num_str.c_str(),
         /* overwrite */ 1);

#if defined(__BIONIC__)
  // These environment variables are needed in case when Bionic is used.
  // b/171754977
  setenv("ANDROID_DATA", DefaultHostArtifactsPath("").c_str(),
         /* overwrite */ 0);
  setenv("ANDROID_TZDATA_ROOT", DefaultHostArtifactsPath("").c_str(),
         /* overwrite */ 0);
  setenv("ANDROID_ROOT", DefaultHostArtifactsPath("").c_str(),
         /* overwrite */ 0);
#endif

  auto assembler_input = WriteFiles(AvailableFilesReport());
  std::string assembler_output;
  auto assemble_ret =
      InvokeAssembler(assembler_input, assembler_output,
                      forwarder.ArgvForSubprocess(AssemblerPath(), args));

  if (assemble_ret != 0) {
    LOG(ERROR) << "assemble_cvd returned " << assemble_ret;
    return assemble_ret;
  } else {
    LOG(DEBUG) << "assemble_cvd exited successfully.";
  }

  std::string conf_path;
  for (const auto& line : android::base::Tokenize(assembler_output, "\n")) {
    if (android::base::EndsWith(line, "cuttlefish_config.json")) {
      conf_path = line;
    }
  }
  CHECK(!conf_path.empty()) << "could not find config";
  auto config = CuttlefishConfig::GetFromFile(conf_path);
  CHECK(config) << "Could not load config object";
  setenv(kCuttlefishConfigEnvVarName, conf_path.c_str(), /* overwrite */ true);

  std::vector<Subprocess> runners;
  for (const auto& instance : config->Instances()) {
    SharedFD runner_stdin = SharedFD::Open("/dev/null", O_RDONLY);
    CHECK(runner_stdin->IsOpen()) << runner_stdin->StrError();
    setenv(kCuttlefishInstanceEnvVarName, instance.id().c_str(),
           /* overwrite */ 1);

    auto run_proc = StartRunner(std::move(runner_stdin), *config, instance,
                                forwarder.ArgvForSubprocess(RunnerPath()));
    runners.push_back(std::move(run_proc));
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

}  // namespace
}  // namespace cuttlefish

int main(int argc, char** argv) {
  return cuttlefish::CvdInternalStartMain(argc, argv);
}
