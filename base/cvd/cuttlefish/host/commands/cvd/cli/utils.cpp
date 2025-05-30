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

#include "cuttlefish/host/commands/cvd/cli/utils.h"

#include <iostream>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <fmt/format.h>

#include "cuttlefish/common/libs/fs/shared_fd.h"
#include "cuttlefish/common/libs/utils/contains.h"
#include "cuttlefish/common/libs/utils/files.h"
#include "cuttlefish/common/libs/utils/result.h"
#include "cuttlefish/host/commands/cvd/instances/config_path.h"
#include "cuttlefish/host/commands/cvd/utils/common.h"
#include "cuttlefish/host/libs/config/config_constants.h"

namespace cuttlefish {

Result<void> CheckProcessExitedNormally(siginfo_t infop,
                                        const int expected_exit_code) {
  if (infop.si_code == CLD_EXITED && infop.si_status == expected_exit_code) {
    return {};
  }

  if (infop.si_code == CLD_EXITED) {
    return CF_ERRF("Exited with code '{}'", infop.si_status);
  } else if (infop.si_code == CLD_KILLED) {
    return CF_ERRF("Exited with signal '{}'", infop.si_status);
  } else {
    return CF_ERRF("Quit with code '{}'", infop.si_status);
  }
}

Result<Command> ConstructCommand(const ConstructCommandParam& param) {
  Command command(param.command_name);
  command.SetExecutable(param.bin_path);
  for (const std::string& arg : param.args) {
    command.AddParameter(arg);
  }
  // Set CuttlefishConfig path based on assembly dir,
  // used by subcommands when locating the CuttlefishConfig.
  if (param.envs.count(cuttlefish::kCuttlefishConfigEnvVarName) == 0) {
    auto config_path = GetCuttlefishConfigPath(param.home);
    if (config_path.ok()) {
      command.AddEnvironmentVariable(cuttlefish::kCuttlefishConfigEnvVarName,
                                     *config_path);
    }
  }
  for (auto& it : param.envs) {
    command.UnsetFromEnvironment(it.first);
    command.AddEnvironmentVariable(it.first, it.second);
  }

  if (!param.working_dir.empty()) {
    auto fd =
        SharedFD::Open(param.working_dir, O_RDONLY | O_PATH | O_DIRECTORY);
    CF_EXPECT(fd->IsOpen(), "Couldn't open \"" << param.working_dir
                                               << "\": " << fd->StrError());
    command.SetWorkingDirectory(fd);
  }
  return {std::move(command)};
}

Result<Command> ConstructCvdHelpCommand(
    const std::string& bin_file, cvd_common::Envs envs,
    const std::vector<std::string>& subcmd_args,
    const CommandRequest& request) {
  auto client_pwd = CurrentDirectory();
  const auto home = (Contains(envs, "HOME") ? envs.at("HOME") : client_pwd);
  cvd_common::Envs envs_copy{envs};
  envs_copy["HOME"] = AbsolutePath(home);
  auto android_host_out = CF_EXPECT(AndroidHostPath(envs));
  const auto bin_path = android_host_out + "/bin/" + bin_file;
  envs_copy[kAndroidHostOut] = android_host_out;
  envs_copy[kAndroidSoongHostOut] = android_host_out;
  ConstructCommandParam construct_cmd_param{.bin_path = bin_path,
                                            .home = home,
                                            .args = subcmd_args,
                                            .envs = std::move(envs_copy),
                                            .working_dir = client_pwd,
                                            .command_name = bin_file
  };
  Command help_command = CF_EXPECT(ConstructCommand(construct_cmd_param));
  return help_command;
}

Result<Command> ConstructCvdGenericNonHelpCommand(
    const ConstructNonHelpForm& request_form, const CommandRequest& request) {
  cvd_common::Envs envs{request_form.envs};
  envs["HOME"] = request_form.home;
  envs[kAndroidHostOut] = request_form.android_host_out;
  envs[kAndroidSoongHostOut] = request_form.android_host_out;
  const auto bin_path = ConcatToString(request_form.android_host_out, "/bin/",
                                       request_form.bin_file);

  if (request_form.verbose) {
    std::stringstream verbose_stream;
    verbose_stream << "HOME=" << request_form.home << " ";
    verbose_stream << kAndroidHostOut << "=" << envs.at(kAndroidHostOut) << " "
                   << kAndroidSoongHostOut << "="
                   << envs.at(kAndroidSoongHostOut) << " ";
    verbose_stream << bin_path << "\\" << std::endl;
    for (const auto& cmd_arg : request_form.cmd_args) {
      verbose_stream << cmd_arg << " ";
    }
    if (!request_form.cmd_args.empty()) {
      // remove trailing " ", and add a new line
      verbose_stream.seekp(-1, std::ios_base::end);
      verbose_stream << std::endl;
    }
    std::cerr << verbose_stream.rdbuf();
  }
  ConstructCommandParam construct_cmd_param{
      .bin_path = bin_path,
      .home = request_form.home,
      .args = request_form.cmd_args,
      .envs = envs,
      .working_dir = CurrentDirectory(),
      .command_name = request_form.bin_file
  };
  return CF_EXPECT(ConstructCommand(construct_cmd_param));
}

static constexpr char kTerminalBoldRed[] = "\033[0;1;31m";
static constexpr char kTerminalCyan[] = "\033[0;36m";
static constexpr char kTerminalRed[] = "\033[0;31m";
static constexpr char kTerminalReset[] = "\033[0m";

std::string_view TerminalColors::Reset() const {
  return is_tty_ ? kTerminalReset : "";
}

std::string_view TerminalColors::BoldRed() const {
  return is_tty_ ? kTerminalBoldRed : "";
}

std::string_view TerminalColors::Red() const {
  return is_tty_ ? kTerminalRed : "";
}

std::string_view TerminalColors::Cyan() const {
  return is_tty_ ? kTerminalCyan : "";
}

std::string NoGroupMessage(const CommandRequest& request) {
  TerminalColors colors(isatty(1));
  return fmt::format("Command `{}{}{}` is not applicable: {}{}{}", colors.Red(),
                     fmt::join(request.SubcommandArguments(), " "),
                     colors.Reset(), colors.BoldRed(), "no device",
                     colors.Reset());
}

}  // namespace cuttlefish
