//
// Copyright (C) 2023 The Android Open Source Project
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

#include "host/commands/cvd/unittests/server/cmd_runner.h"

#include <android-base/strings.h>

namespace cuttlefish {

CmdResult::CmdResult(const std::string& stdout_str,
                     const std::string& stderr_str, const int ret_code)
    : stdout_{stdout_str}, stderr_{stderr_str}, code_{ret_code} {}

CmdResult CmdRunner::Run(const cvd_common::Args& args,
                         const cvd_common::Envs& envs) {
  if (args.empty() || args.front().empty()) {
    return CmdResult("", "Empty or invalid command", -1);
  }
  const auto& cmd = args.front();
  cvd_common::Args cmd_args{args.begin() + 1, args.end()};
  CmdRunner cmd_runner(Command(cmd), cmd_args, envs);
  return cmd_runner.Run();
}

CmdResult CmdRunner::Run(const std::string& args,
                         const cvd_common::Envs& envs) {
  return CmdRunner::Run(android::base::Tokenize(args, " "), envs);
}

CmdRunner::CmdRunner(Command&& cmd, const cvd_common::Args& args,
                     const cvd_common::Envs& envs)
    : cmd_(std::move(cmd)) {
  for (const auto& arg : args) {
    cmd_.AddParameter(arg);
  }
  for (const auto& [key, value] : envs) {
    cmd_.AddEnvironmentVariable(key, value);
  }
}

CmdResult CmdRunner::Run() {
  std::string stdout_str;
  std::string stderr_str;
  auto ret_code =
      RunWithManagedStdio(std::move(cmd_), nullptr, std::addressof(stdout_str),
                          std::addressof(stderr_str));
  return CmdResult(stdout_str, stderr_str, ret_code);
}

}  // namespace cuttlefish
