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

#include <fcntl.h>
#include <stdlib.h>
#include <sys/prctl.h>
#include <unistd.h>

#include <cerrno>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/base/log_severity.h"
#include "absl/flags/flag.h"
#include "absl/flags/parse.h"
#include "absl/log/check.h"
#include "absl/log/globals.h"
#include "absl/log/initialize.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "sandboxed_api/util/fileops.h"
#include "sandboxed_api/util/path.h"

#include "cuttlefish/host/commands/process_sandboxer/logs.h"
#include "cuttlefish/host/commands/process_sandboxer/pidfd.h"
#include "cuttlefish/host/commands/process_sandboxer/policies.h"
#include "cuttlefish/host/commands/process_sandboxer/sandbox_manager.h"

inline constexpr char kCuttlefishConfigEnvVarName[] = "CUTTLEFISH_CONFIG_FILE";

ABSL_FLAG(std::string, assembly_dir, "", "cuttlefish/assembly build dir");
ABSL_FLAG(std::string, host_artifacts_path, "", "Host exes and libs");
ABSL_FLAG(std::string, environments_dir, "", "Cross-instance environment dir");
ABSL_FLAG(std::string, guest_image_path, "", "Directory with `system.img`");
ABSL_FLAG(std::string, sandboxer_log_dir, "", "Where to write log files");
ABSL_FLAG(std::vector<std::string>, log_files, std::vector<std::string>(),
          "File paths outside the sandbox to write logs to");
ABSL_FLAG(std::string, runtime_dir, "",
          "Working directory of host executables");
ABSL_FLAG(bool, verbose_stderr, false, "Write debug messages to stderr");

namespace cuttlefish::process_sandboxer {
namespace {

using sapi::file::CleanPath;
using sapi::file::JoinPath;
using sapi::file_util::fileops::FDCloser;

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

  if (prctl(PR_SET_CHILD_SUBREAPER, 1) < 0) {
    return absl::ErrnoToStatus(errno, "prctl(PR_SET_CHILD_SUBREAPER failed");
  }

  std::string tmp_dir(FromEnv("TMPDIR").value_or("/tmp"));
  tmp_dir += "/process_sandboxer.XXXXXX";
  if (mkdtemp(tmp_dir.data()) == nullptr) {
    return absl::ErrnoToStatus(errno, "mkdtemp failed");
  }

  HostInfo host{
      .assembly_dir = CleanPath(absl::GetFlag(FLAGS_assembly_dir)),
      .cuttlefish_config_path =
          CleanPath(FromEnv(kCuttlefishConfigEnvVarName).value_or("")),
      .environments_dir = CleanPath(absl::GetFlag(FLAGS_environments_dir)),
      .guest_image_path = CleanPath(absl::GetFlag(FLAGS_guest_image_path)),
      .host_artifacts_path =
          CleanPath(absl::GetFlag(FLAGS_host_artifacts_path)),
      .log_dir = CleanPath(absl::GetFlag(FLAGS_sandboxer_log_dir)),
      .runtime_dir = CleanPath(absl::GetFlag(FLAGS_runtime_dir)),
      .tmp_dir = tmp_dir,
  };

  // TODO: schuffelen - try to guess these from the cvd_internal_start arguments

  std::optional<std::string_view> home = FromEnv("HOME");

  // CleanPath will set empty strings to ".", so consider that the unset value.
  if (host.assembly_dir == "." && home.has_value()) {
    host.assembly_dir = CleanPath(JoinPath(*home, "cuttlefish", "assembly"));
  }
  if (host.cuttlefish_config_path == "." && home.has_value()) {
    host.cuttlefish_config_path = CleanPath(
        JoinPath(*home, "cuttlefish", "assembly", "cuttlefish_config.json"));
  }
  if (host.environments_dir == "." && home.has_value()) {
    host.environments_dir =
        CleanPath(JoinPath(*home, "cuttlefish", "environments"));
  }
  if (host.log_dir == "." && home.has_value()) {
    host.log_dir =
        CleanPath(JoinPath(*home, "cuttlefish", "instances", "cvd-1", "logs"));
  }
  if (host.runtime_dir == "." && home.has_value()) {
    host.runtime_dir =
        CleanPath(JoinPath(*home, "cuttlefish", "instances", "cvd-1"));
  }

  std::optional<std::string_view> product_out = FromEnv("ANDROID_PRODUCT_OUT");

  if (host.guest_image_path == ".") {
    if (product_out.has_value()) {
      host.guest_image_path = CleanPath(*product_out);
    } else if (home.has_value()) {
      host.guest_image_path = CleanPath(*home);
    }
  }

  std::optional<std::string_view> host_out = FromEnv("ANDROID_HOST_OUT");

  if (host.host_artifacts_path == ".") {
    if (host_out.has_value()) {
      host.host_artifacts_path = CleanPath(*host_out);
    } else if (home.has_value()) {
      host.host_artifacts_path = CleanPath(*home);
    }
  }

  absl::Status dir_creation = host.EnsureOutputDirectoriesExist();
  if (!dir_creation.ok()) {
    return dir_creation;
  }

  absl::Status logs_status;
  if (absl::GetFlag(FLAGS_log_files).empty()) {
    std::string default_log_path = JoinPath(host.log_dir, "launcher.log");
    unlink(default_log_path.c_str());  // Clean from previous run
    logs_status = LogToFiles({default_log_path});
  } else {
    logs_status = LogToFiles(absl::GetFlag(FLAGS_log_files));
    if (!logs_status.ok()) {
      return logs_status;
    }
  }
  if (!logs_status.ok()) {
    return logs_status;
  }

  VLOG(1) << host;

  setenv("LD_LIBRARY_PATH", JoinPath(host.host_artifacts_path, "lib64").c_str(),
         1);

  if (args.size() < 2) {
    std::string err = absl::StrCat("Wanted argv.size() > 1, was ", args.size());
    return absl::InvalidArgumentError(err);
  }
  std::string exe = CleanPath(args[1]);
  std::vector<std::string> exe_argv(++args.begin(), args.end());

  if (absl::EndsWith(exe, "cvd_internal_start")) {
    setenv("TMPDIR", host.tmp_dir.c_str(), 1);
  }

  auto sandbox_manager_res = SandboxManager::Create(std::move(host));
  if (!sandbox_manager_res.ok()) {
    return sandbox_manager_res.status();
  }
  std::unique_ptr<SandboxManager> manager = std::move(*sandbox_manager_res);

  std::vector<std::pair<FDCloser, int>> fds;
  for (int i = 0; i <= 2; i++) {
    auto duped = fcntl(i, F_DUPFD_CLOEXEC, 0);
    if (duped < 0) {
      static constexpr char kErr[] = "Failed to `dup` stdio file descriptor";
      return absl::ErrnoToStatus(errno, kErr);
    }
    fds.emplace_back(FDCloser(duped), i);
  }

  std::vector<std::string> this_env;
  for (size_t i = 0; environ[i] != nullptr; i++) {
    this_env.emplace_back(environ[i]);
  }

  absl::Status status = manager->RunProcess(std::nullopt, std::move(exe_argv),
                                            std::move(fds), this_env);
  if (!status.ok()) {
    return status;
  }

  while (manager->Running()) {
    absl::Status iter = manager->Iterate();
    if (!iter.ok()) {
      LOG(ERROR) << "Error in SandboxManager::Iterate: " << iter.ToString();
    }
  }

  absl::StatusOr<PidFd> self_pidfd = PidFd::FromRunningProcess(getpid());
  if (!self_pidfd.ok()) {
    return self_pidfd.status();
  }

  return self_pidfd->HaltChildHierarchy();
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
