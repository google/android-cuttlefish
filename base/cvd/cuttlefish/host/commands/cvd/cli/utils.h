/*
 * Copyright (C) 2022 The Android Open Source Project
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

#pragma once

#include <signal.h>

#include <string>
#include <string_view>
#include <vector>

#include "common/libs/utils/result.h"
#include "common/libs/utils/subprocess.h"
#include "host/commands/cvd/cli/command_request.h"
#include "host/commands/cvd/cli/types.h"

namespace cuttlefish {

Result<void> CheckProcessExitedNormally(siginfo_t infop);

struct ConstructCommandParam {
  const std::string& bin_path;
  const std::string& home;
  const std::vector<std::string>& args;
  const cvd_common::Envs& envs;
  const std::string& working_dir;
  const std::string& command_name;
};
Result<Command> ConstructCommand(const ConstructCommandParam& cmd_param);

// Constructs a command for cvd whatever --help or --help-related-option
Result<Command> ConstructCvdHelpCommand(const std::string& bin_file,
                                        cvd_common::Envs envs,
                                        const cvd_common::Args& _args,
                                        const CommandRequest& request);

// Constructs a command for cvd non-start-op
struct ConstructNonHelpForm {
  std::string bin_file;
  cvd_common::Envs envs;
  cvd_common::Args cmd_args;
  std::string android_host_out;
  std::string home;
  bool verbose;
};
Result<Command> ConstructCvdGenericNonHelpCommand(
    const ConstructNonHelpForm& request_form, const CommandRequest& request);

// Call this when there is no instance group is running
// The function does not verify that.
std::string NoGroupMessage(const CommandRequest& request);

class TerminalColors {
 public:
  TerminalColors(bool is_tty): is_tty_(is_tty){}
  std::string_view Reset() const;
  std::string_view BoldRed() const;
  std::string_view Red() const;
  std::string_view Cyan() const;
 private:
  bool is_tty_;
};

}  // namespace cuttlefish
