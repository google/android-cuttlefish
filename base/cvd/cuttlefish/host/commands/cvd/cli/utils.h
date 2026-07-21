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
#include <vector>

#include "cuttlefish/flag_parser/flag.h"
#include "cuttlefish/host/commands/cvd/cli/command_request.h"
#include "cuttlefish/process/command.h"
#include "cuttlefish/result/result.h"

namespace cuttlefish {

Result<void> CheckProcessExitedNormally(siginfo_t infop,
                                        int expected_exit_code = 0);

struct ConstructCommandParam {
  const std::string& bin_path;
  const std::string& home;
  const std::vector<std::string>& args;
  const std::unordered_map<std::string, std::string>& envs;
  const std::string& working_dir;
  const std::string& command_name;
};
Result<Command> ConstructCommand(const ConstructCommandParam& cmd_param);

// Constructs a command for cvd whatever --help or --help-related-option
Result<Command> ConstructCvdHelpCommand(
    const std::string& bin_file,
    std::unordered_map<std::string, std::string> envs,
    const std::vector<std::string>& _args, const CommandRequest& request);

Result<Command> ConstructSiblingHelpCommand(
    const std::string& bin_name,
    const std::unordered_map<std::string, std::string>& env,
    const std::vector<std::string>& subcmd_args);

// Constructs a command for cvd non-start-op
struct ConstructNonHelpForm {
  std::string bin_file;
  std::unordered_map<std::string, std::string> envs;
  std::vector<std::string> cmd_args;
  std::string android_host_out;
  std::string home;
  bool verbose;
};
Result<Command> ConstructCvdGenericNonHelpCommand(
    const ConstructNonHelpForm& request_form, const CommandRequest& request);

// Returns the flags supported by a sibling command.
// A sibling command is one whose executable resides in the same directory as
// the current process'. Flags are obtained by running the sibling command with
// the given arguments and environment plus the --helpxml flag.
// Gflags-specific flags (except --help) are filtered out to keep the output
// size reasonable.
Result<std::vector<Flag>> GetSiblingCommandFlags(
    const std::string& bin_name,
    const std::unordered_map<std::string, std::string>& env,
    std::vector<std::string> args);

// Call this when there is no instance group is running
// The function does not verify that.
std::string NoGroupMessage(const CommandRequest& request);

struct TerminalSize {
  int rows;
  int columns;
};
Result<TerminalSize> GetTerminalSize();

std::vector<std::string> ExpandProductPaths(const std::string& product_path,
                                            size_t num_instances);

}  // namespace cuttlefish
