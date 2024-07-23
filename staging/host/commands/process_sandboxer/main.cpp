/*
 * Copyright (C) 2024 The Android Open Source Project
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

#include <stdlib.h>

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <absl/flags/flag.h>
#include <absl/flags/parse.h>
#include <absl/log/check.h>
#include <absl/log/globals.h>
#include <absl/log/initialize.h>
#include <absl/log/log.h>
#include <absl/status/status.h>
#include <absl/strings/numbers.h>
#include <absl/strings/str_cat.h>
#include <sandboxed_api/util/path.h>

#include "host/commands/process_sandboxer/logs.h"
#include "host/commands/process_sandboxer/policies.h"
#include "host/commands/process_sandboxer/sandbox_manager.h"

inline constexpr char kCuttlefishConfigEnvVarName[] = "CUTTLEFISH_CONFIG_FILE";

ABSL_FLAG(std::string, host_artifacts_path, "", "Host exes and libs");
ABSL_FLAG(std::string, log_dir, "", "Where to write log files");
ABSL_FLAG(std::vector<std::string>, inherited_fds, std::vector<std::string>(),
          "File descriptors to keep in the sandbox");
ABSL_FLAG(std::string, runtime_dir, "",
          "Working directory of host executables");
ABSL_FLAG(std::vector<std::string>, log_files, std::vector<std::string>(),
          "File paths outside the sandbox to write logs to");
ABSL_FLAG(bool, verbose_stderr, false, "Write debug messages to stderr");

namespace cuttlefish::process_sandboxer {
namespace {

using sapi::file::CleanPath;
using sapi::file::JoinPath;

std::optional<std::string_view> FromEnv(const std::string& name) {
  char* value = getenv(name.c_str());
  return value == NULL ? std::optional<std::string_view>() : value;
}

absl::Status ProcessSandboxerMain(int argc, char** argv) {
  std::vector<char*> args = absl::ParseCommandLine(argc, argv);
  /* When building in AOSP, the flags in absl/log/flags.cc are missing. This
   * uses the absl/log/globals.h interface to log ERROR severity to stderr, and
   * write all LOG and VLOG(1) messages to log sinks pointing to log files. */
  absl::InitializeLog();
  if (absl::GetFlag(FLAGS_verbose_stderr)) {
    absl::SetStderrThreshold(absl::LogSeverity::kError);
  } else {
    absl::SetStderrThreshold(absl::LogSeverity::kInfo);
  }
  absl::EnableLogPrefix(true);
  absl::SetGlobalVLogLevel(1);

  absl::Status logs_status = LogToFiles(absl::GetFlag(FLAGS_log_files));
  if (!logs_status.ok()) {
    return logs_status;
  }

  VLOG(1) << "Entering ProcessSandboxerMain";

  HostInfo host;
  host.artifacts_path = CleanPath(absl::GetFlag(FLAGS_host_artifacts_path));
  host.cuttlefish_config_path =
      CleanPath(FromEnv(kCuttlefishConfigEnvVarName).value_or(""));
  host.log_dir = CleanPath(absl::GetFlag(FLAGS_log_dir));
  host.runtime_dir = CleanPath(absl::GetFlag(FLAGS_runtime_dir));
  setenv("LD_LIBRARY_PATH", JoinPath(host.artifacts_path, "lib64").c_str(), 1);

  if (args.size() < 2) {
    std::string err = absl::StrCat("Wanted argv.size() > 1, was ", args.size());
    return absl::InvalidArgumentError(err);
  }
  std::string exe = CleanPath(args[1]);
  std::vector<std::string> exe_argv(++args.begin(), args.end());

  auto sandbox_manager_res = SandboxManager::Create(std::move(host));
  if (!sandbox_manager_res.ok()) {
    return sandbox_manager_res.status();
  }
  std::unique_ptr<SandboxManager> manager = std::move(*sandbox_manager_res);

  std::map<int, int> fds;
  for (const std::string& inherited_fd : absl::GetFlag(FLAGS_inherited_fds)) {
    int fd;
    if (!absl::SimpleAtoi(inherited_fd, &fd)) {
      std::string error = absl::StrCat("inherited_fd not int: ", inherited_fd);
      return absl::InvalidArgumentError(error);
    }
    fds[fd] = fd;  // RunProcess will close these
  }

  absl::Status run = manager->RunProcess(std::move(exe_argv), std::move(fds));
  if (!run.ok()) {
    return run;
  }

  while (manager->Running()) {
    absl::Status iter = manager->Iterate();
    if (!iter.ok()) {
      LOG(ERROR) << "Error in SandboxManager::Iterate: " << iter.ToString();
    }
  }
  return absl::OkStatus();
}

}  // namespace
}  // namespace cuttlefish::process_sandboxer

int main(int argc, char** argv) {
  auto status = cuttlefish::process_sandboxer::ProcessSandboxerMain(argc, argv);
  if (status.ok()) {
    VLOG(1) << "process_sandboxer exiting normally";
    return 0;
  }
  LOG(ERROR) << status.ToString();
  return status.raw_code();
}
