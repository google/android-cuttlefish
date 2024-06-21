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

#include "absl/flags/flag.h"
#include "absl/flags/parse.h"
#include "absl/log/check.h"
#include "absl/log/initialize.h"
#include "absl/strings/numbers.h"
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-parameter"
#include "sandboxed_api/sandbox2/executor.h"
#include "sandboxed_api/sandbox2/sandbox2.h"
#pragma clang diagnostic pop
#include "sandboxed_api/util/path.h"

#include "host/commands/process_sandboxer/policies.h"

inline constexpr char kCuttlefishConfigEnvVarName[] = "CUTTLEFISH_CONFIG_FILE";

ABSL_FLAG(std::string, host_artifacts_path, "", "Host exes and libs");
ABSL_FLAG(std::string, log_dir, "", "Where to write log files");
ABSL_FLAG(std::vector<std::string>, inherited_fds, std::vector<std::string>(),
          "File descriptors to keep in the sandbox");

using sapi::file::CleanPath;
using sapi::file::JoinPath;

namespace cuttlefish {

static std::optional<std::string_view> FromEnv(const std::string& name) {
  auto value = getenv(name.c_str());
  return value == NULL ? std::optional<std::string_view>() : value;
}

int ProcessSandboxerMain(int argc, char** argv) {
  absl::InitializeLog();
  auto args = absl::ParseCommandLine(argc, argv);

  HostInfo host;
  host.artifacts_path = CleanPath(absl::GetFlag(FLAGS_host_artifacts_path));
  host.cuttlefish_config_path =
      CleanPath(FromEnv(kCuttlefishConfigEnvVarName).value_or(""));
  host.log_dir = CleanPath(absl::GetFlag(FLAGS_log_dir));
  setenv("LD_LIBRARY_PATH", JoinPath(host.artifacts_path, "lib64").c_str(), 1);

  CHECK_GE(args.size(), 1);
  auto exe = CleanPath(args[1]);
  std::vector<std::string> exe_argv(++args.begin(), args.end());
  auto executor = std::make_unique<sandbox2::Executor>(exe, exe_argv);

  for (const auto& inherited_fd : absl::GetFlag(FLAGS_inherited_fds)) {
    int fd;
    CHECK(absl::SimpleAtoi(inherited_fd, &fd));
    executor->ipc()->MapFd(fd, fd);  // Will close `fd` in this process
  }

  sandbox2::Sandbox2 sb(std::move(executor), PolicyForExecutable(host, exe));

  auto res = sb.Run();
  CHECK_EQ(res.final_status(), sandbox2::Result::OK) << res.ToString();
  return 0;
}

}  // namespace cuttlefish

int main(int argc, char** argv) {
  return cuttlefish::ProcessSandboxerMain(argc, argv);
}
