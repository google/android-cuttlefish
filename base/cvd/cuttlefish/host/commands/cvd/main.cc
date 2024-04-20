/*
 * Copyright (C) 2021 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <sys/resource.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cstdio>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <android-base/file.h>
#include <android-base/logging.h>
#include <android-base/scopeguard.h>
#include <android-base/strings.h>

#include "common/libs/utils/contains.h"
#include "common/libs/utils/environment.h"
#include "common/libs/utils/files.h"
#include "common/libs/utils/subprocess.h"
#include "host/commands/cvd/client.h"
#include "host/commands/cvd/cvd.h"
#include "host/commands/cvd/common_utils.h"
#include "host/commands/cvd/fetch/fetch_cvd.h"
#include "host/commands/cvd/flag.h"
#include "host/commands/cvd/frontline_parser.h"
// TODO(315772518) Re-enable once metrics send is reenabled
// #include "host/commands/cvd/metrics/cvd_metrics_api.h"
#include "host/commands/cvd/run_server.h"
#include "host/commands/cvd/server_constants.h"
#include "host/libs/config/host_tools_version.h"

namespace cuttlefish {
namespace {

/**
 * Returns --verbosity value if ever exist in the entire commandline args
 *
 * Note that this will also pick up from the subtool arguments:
 *  e.g. cvd start --verbosity=DEBUG
 *
 * This may be incorrect as the verbosity should be ideally applied to the
 * launch_cvd/cvd_internal_start only.
 *
 * However, parsing the --verbosity flag only from the driver is quite
 * complicated as we do not know the full list of the subcommands,
 * the subcommands flags, and even the selector/driver flags.
 *
 * Thus, we live with the corner case for now.
 */
android::base::LogSeverity CvdVerbosityOption(const int argc, char** argv) {
  cvd_common::Args all_args = ArgsToVec(argc, argv);
  std::string verbosity_flag_value;
  std::vector<Flag> verbosity_flag{
      GflagsCompatFlag("verbosity", verbosity_flag_value)};
  if (!ConsumeFlags(verbosity_flag, all_args).ok()) {
    LOG(ERROR) << "Verbosity flag parsing failed, so use the default value.";
    return GetMinimumVerbosity();
  }
  if (verbosity_flag_value.empty()) {
    return GetMinimumVerbosity();
  }
  auto encoded_verbosity = EncodeVerbosity(verbosity_flag_value);
  return (encoded_verbosity.ok() ? *encoded_verbosity : GetMinimumVerbosity());
}

/**
 * Terminates a cvd server listening on "cvd_server"
 *
 * So far, the server processes across users were listing on the "cvd_server"
 * socket. And, so far, we had one user. Now, we have multiple users. Each
 * server listens to cvd_server_<uid>. The thing is if there is a server process
 * started out of an old executable it will be listening to "cvd_server," and
 * thus we should kill the server process first.
 */
Result<void> KillOldServer() {
  CvdClient client_to_old_server(kCvdDefaultVerbosity, "cvd_server");
  auto result = client_to_old_server.StopCvdServer(/*clear=*/true);
  if (!result.ok()) {
    LOG(ERROR) << "Old server listening on \"cvd_server\" socket "
               << "must be killed first but failed to terminate it.";
    LOG(ERROR) << "Perhaps, try cvd reset -y";
    CF_EXPECT(std::move(result));
  }
  return {};
}

Result<void> EnsureCvdDirectoriesExist() {
  // This is accessed by all users.
  CF_EXPECT(EnsureDirectoryExists(CvdDir(), 0777));
  // This is where the instance database resides.
  CF_EXPECT(EnsureDirectoryExists(PerUserDir(), 0750));

  return {};
}

/**
 * Persist a running server's instance database to the file.
 *
 * It works by asking the server to restart itself using our executable file.
 */
void TryInheritServerDatabase() {
  CvdClient client(kCvdDefaultVerbosity);

  if (!client.ConnectToServer().ok()) {
    LOG(VERBOSE) << "No server found";
    // There seems to be no server running
    return;
  }
  LOG(VERBOSE) << "Asking server to restart";
  auto res = client.RestartServerMatchClient();
  if (!res.ok()) {
    std::cerr << res.error().FormatForEnv() << std::endl;
    std::cerr
        << "Failed to take over resources of running server.\nSome devices may "
           "be running outside cvd's control, consider running cvd reset -y"
        << std::endl;
  }
}

/**
 * Increase the file descriptor limit for this process and its descendants.
 *
 * Crosvm tends to use many file descriptors, especially when running in sandbox
 * mode, sometimes exceeding the default limit.
 */
void IncreaseFileLimit() {
  struct rlimit old_lim;
  // Get old limits
  if (getrlimit(RLIMIT_NOFILE, &old_lim) != 0) {
    auto err = strerror(errno);
    LOG(WARNING) << "Unable to get file limit (" << err
                 << "), virtual devices may not work properly if the limit is "
                    "set too low";
    return;
  }
  LOG(VERBOSE) << "Old limits -> soft limit= " << old_lim.rlim_cur << "\t"
            << " hard limit= " << old_lim.rlim_max;
  // Set new value
  old_lim.rlim_cur = old_lim.rlim_max;
  // Set limits
  if (setrlimit(RLIMIT_NOFILE, &old_lim) != 0) {
    auto err = strerror(errno);
    LOG(WARNING) << "Unable to set file limit (" << err
                 << "), virtual devices may not work properly if the limit is "
                    "set too low";
  }
}

Result<void> CvdMain(int argc, char** argv, char** envp,
                     const android::base::LogSeverity verbosity) {
  CF_EXPECT(EnsureCvdDirectoriesExist());

  CF_EXPECT(KillOldServer());

  cvd_common::Args all_args = ArgsToVec(argc, argv);
  CF_EXPECT(!all_args.empty());

  if (IsServerModeExpected(all_args[0])) {
    // Persist previous server's instance database to file.
    ImportResourcesFromRunningServer(std::move(all_args));
    return {};
  } else {
    // Calling this while in "server mode" causes a deadlock because it tries to
    // connect to its own socket that it hasn't called accept() on (and never
    // will).
    // We could close that socket file descriptor immediately, but then a
    // concurrent execution of the command will not find the socket and proceed
    // as normal without waiting for this process to persist the instance
    // database.
    TryInheritServerDatabase();
  }


  auto env = EnvpToMap(envp);
  // TODO(315772518) Re-enable once metrics send is skipped in a env
  // without network support
  // CvdMetrics::SendCvdMetrics(all_args);

  if (android::base::Basename(all_args[0]) == "fetch_cvd") {
    CF_EXPECT(FetchCvdMain(argc, argv));
    return {};
  }

  IncreaseFileLimit();

  InstanceLockFileManager instance_lockfile_manager;
  auto host_tool_target_manager = NewHostToolTargetManager();
  selector::InstanceDatabase instance_db(InstanceDatabasePath());
  InstanceManager instance_manager(instance_lockfile_manager,
                                   *host_tool_target_manager, instance_db);
  Cvd cvd(verbosity, instance_lockfile_manager, instance_manager,
          *host_tool_target_manager);

  // TODO(b/206893146): Make this decision inside the server.
  if (android::base::Basename(all_args[0]) == "acloud") {
    return cvd.HandleAcloud(all_args, env);
  }
  if (android::base::Basename(all_args[0]) == "cvd") {
    CF_EXPECT(cvd.HandleCvdCommand(all_args, env));
    return {};
  }
  CF_EXPECT(cvd.HandleCommand(all_args, env, {}));

  return {};
}

/**
 * Returns the URL as a colored string
 *
 * If stderr is not terminal, no color.
 * If stderr is a tty, tries to use ".deb" file color
 * If .deb is not available in LS_COLORS, uses .zip
 * color. If none are available, use a default color that
 * is red.
 */
std::string ColoredUrl(const std::string& url) {
  if (!isatty(STDERR_FILENO)) {
    return url;
  }
  std::string coloring_prefix = "\033[01;31m";
  std::string output;
  auto ls_colors = StringFromEnv("LS_COLORS", "");
  std::vector<std::string> colors_vec = android::base::Tokenize(ls_colors, ":");
  std::unordered_map<std::string, std::string> colors;
  for (const auto& color_entry : colors_vec) {
    std::vector<std::string> tokenized =
        android::base::Tokenize(color_entry, "=");
    if (tokenized.size() != 2) {
      continue;
    }
    colors[tokenized.front()] = tokenized.back();
  }

  android::base::ScopeGuard return_action([&coloring_prefix, url, &output]() {
    static constexpr char kRestoreColor[] = "\033[0m";
    output = fmt::format("{}{}{}", coloring_prefix, url, kRestoreColor);
  });
  auto deb_color_itr = colors.find("*.deb");
  auto zip_color_itr = colors.find("*.zip");
  if (deb_color_itr == colors.end() && zip_color_itr == colors.end()) {
    return output;
  }
  coloring_prefix = fmt::format(
      "{}{}m", "\033[",
      (deb_color_itr == colors.end() ? colors["*.zip"] : colors["*.deb"]));
  return output;
}

}  // namespace
}  // namespace cuttlefish

int main(int argc, char** argv, char** envp) {
  android::base::LogSeverity verbosity =
      cuttlefish::CvdVerbosityOption(argc, argv);
  android::base::InitLogging(argv, android::base::StderrLogger);
  // set verbosity for this process
  cuttlefish::SetMinimumVerbosity(verbosity);

  auto result = cuttlefish::CvdMain(argc, argv, envp, verbosity);
  if (result.ok()) {
    return 0;
  } else {
    // TODO: we should not print the stack trace, instead, we should rely on
    // each handler to print the error message directly in the client's
    // std::cerr. We print the stack trace only in the verbose mode.
    std::cerr << result.error().FormatForEnv(isatty(STDERR_FILENO))
              << std::endl;
    // TODO(kwstephenkim): better coloring
    constexpr char kUserReminder[] =
        R"(    If the error above is unclear, please copy the text into an issue at:)";
    constexpr char kCuttlefishBugUrl[] = "http://go/cuttlefish-bug";
    std::cerr << std::endl << kUserReminder << std::endl;
    std::cerr << "        " << cuttlefish::ColoredUrl(kCuttlefishBugUrl)
              << std::endl
              << std::endl;
    return -1;
  }
}
