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

#include "host/commands/cvd/cli/utils.h"

#include <fmt/core.h>

#include "common/libs/fs/shared_fd.h"
#include "common/libs/utils/contains.h"
#include "common/libs/utils/files.h"
#include "common/libs/utils/flag_parser.h"
#include "cuttlefish/host/commands/cvd/cli/utils.h"
#include "host/commands/cvd/instances/instance_manager.h"
#include "host/commands/cvd/utils/common.h"
#include "host/libs/config/config_constants.h"

namespace cuttlefish {

Result<void> CheckProcessExitedNormally(siginfo_t infop) {
  if (infop.si_code == CLD_EXITED && infop.si_status == 0) {
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
    auto config_path = InstanceManager::GetCuttlefishConfigPath(param.home);
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
  const auto host_artifacts_path = envs.at("ANDROID_HOST_OUT");
  const auto bin_path = host_artifacts_path + "/bin/" + bin_file;
  auto client_pwd = CurrentDirectory();
  const auto home = (Contains(envs, "HOME") ? envs.at("HOME") : client_pwd);
  cvd_common::Envs envs_copy{envs};
  envs_copy["HOME"] = AbsolutePath(home);
  auto android_host_out = CF_EXPECT(AndroidHostPath(envs));
  envs[kAndroidHostOut] = android_host_out;
  envs[kAndroidSoongHostOut] = android_host_out;
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

/*
 * From external/gflags/src, commit:
 *  061f68cd158fa658ec0b9b2b989ed55764870047
 *
 */
constexpr static std::array help_bool_opts{
    "help", "helpfull", "helpshort", "helppackage", "helpxml", "version", "h"};
constexpr static std::array help_str_opts{
    "helpon",
    "helpmatch",
};

Result<bool> IsHelpSubcmd(const std::vector<std::string>& args) {
  std::vector<std::string> copied_args(args);
  std::vector<Flag> flags;
  flags.reserve(help_bool_opts.size() + help_str_opts.size());
  bool bool_value_placeholder = false;
  std::string str_value_placeholder;
  for (const auto bool_opt : help_bool_opts) {
    flags.emplace_back(GflagsCompatFlag(bool_opt, bool_value_placeholder));
  }
  for (const auto str_opt : help_str_opts) {
    flags.emplace_back(GflagsCompatFlag(str_opt, str_value_placeholder));
  }
  CF_EXPECT(ConsumeFlags(flags, copied_args));
  // if there was any match, some in copied_args were consumed.
  return (args.size() != copied_args.size());
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
                     fmt::join(request.Args(), " "), colors.Reset(),
                     colors.BoldRed(), "no device", colors.Reset());
}

Result<cvd::Response> NoTTYResponse(const CommandRequest& request) {
  cvd::Response response;
  response.mutable_command_response();
  response.mutable_status()->set_code(cvd::Status::OK);
  const uid_t uid = getuid();
  TerminalColors colors(isatty(1));
  auto notice = fmt::format(
      "Command `{}{}{}` is not applicable:\n  {}{}{} (uid: '{}{}{}')",
      colors.Red(), fmt::join(request.Args(), " "), colors.Reset(),
      colors.BoldRed(),
      "No terminal/tty for selecting one of multiple Cuttlefish groups",
      colors.Reset(), colors.Cyan(), uid, colors.Reset());
  std::cout << notice << "\n";
  response.mutable_status()->set_message(notice);
  return response;
}

}  // namespace cuttlefish
