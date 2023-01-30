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

#pragma once

#include <memory>
#include <string>
#include <type_traits>
#include <vector>

#include "common/libs/utils/contains.h"
#include "common/libs/utils/environment.h"
#include "common/libs/utils/files.h"
#include "common/libs/utils/subprocess.h"
#include "host/commands/cvd/types.h"

namespace cuttlefish {

class CmdResult {
 public:
  CmdResult(const std::string& stdout, const std::string& stderr,
            const int ret_code);
  const std::string& Stdout() const { return stdout_; }
  const std::string& Stderr() const { return stderr_; }
  int Code() const { return code_; }
  bool Success() const { return code_ == 0; }

 private:
  std::string stdout_;
  std::string stderr_;
  int code_;
};

class CmdRunner {
 public:
  template <
      typename... CmdArgs,
      typename std::enable_if<(sizeof...(CmdArgs) >= 1), bool>::type = true>
  static CmdResult Run(const std::string& exec, const cvd_common::Envs& envs,
                       CmdArgs&&... cmd_args) {
    cvd_common::Args args;
    args.reserve(sizeof...(CmdArgs));
    (args.emplace_back(std::forward<CmdArgs>(cmd_args)), ...);
    CmdRunner cmd_runner(Command(exec), args, envs);
    return cmd_runner.Run();
  }
  static CmdResult Run(const cvd_common::Args& args,
                       const cvd_common::Envs& envs);
  static CmdResult Run(const std::string& args, const cvd_common::Envs& envs);

 private:
  CmdRunner(Command&& cmd, const cvd_common::Args& args,
            const cvd_common::Envs& envs);

  CmdResult Run();

  Command cmd_;
};

}  // namespace cuttlefish
