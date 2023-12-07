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

#include "host/commands/cvd/server_command/utils.h"

#include "common/libs/utils/contains.h"
#include "common/libs/utils/files.h"
#include "common/libs/utils/flag_parser.h"
#include "common/libs/utils/users.h"
#include "host/commands/cvd/instance_manager.h"
#include "host/commands/cvd/server.h"
#include "host/libs/config/cuttlefish_config.h"

namespace cuttlefish {

CommandInvocation ParseInvocation(const cvd::Request& request) {
  CommandInvocation invocation;
  if (request.contents_case() != cvd::Request::ContentsCase::kCommandRequest) {
    return invocation;
  }
  if (request.command_request().args_size() == 0) {
    return invocation;
  }
  for (const std::string& arg : request.command_request().args()) {
    invocation.arguments.push_back(arg);
  }
  invocation.arguments[0] = cpp_basename(invocation.arguments[0]);
  if (invocation.arguments[0] == "cvd") {
    invocation.command = invocation.arguments[1];
    invocation.arguments.erase(invocation.arguments.begin());
    invocation.arguments.erase(invocation.arguments.begin());
  } else {
    invocation.command = invocation.arguments[0];
    invocation.arguments.erase(invocation.arguments.begin());
  }
  return invocation;
}

PreconditionVerification VerifyPrecondition(const RequestWithStdio& request) {
  PreconditionVerification verification_result;
  if (!request.Credentials()) {
    verification_result.error_message =
        "ucred is not available while it is necessary.";
    return verification_result;
  }
  if (!Contains(request.Message().command_request().env(),
                "ANDROID_HOST_OUT")) {
    verification_result.error_message =
        "ANDROID_HOST_OUT in client environment is invalid.";
    return verification_result;
  }
  verification_result.is_ok = true;
  return verification_result;
}

cuttlefish::cvd::Response ResponseFromSiginfo(siginfo_t infop) {
  cvd::Response response;
  response.mutable_command_response();  // set oneof field
  auto& status = *response.mutable_status();
  if (infop.si_code == CLD_EXITED && infop.si_status == 0) {
    status.set_code(cvd::Status::OK);
    return response;
  }

  status.set_code(cvd::Status::INTERNAL);
  std::string status_code_str = std::to_string(infop.si_status);
  if (infop.si_code == CLD_EXITED) {
    status.set_message("Exited with code " + status_code_str);
  } else if (infop.si_code == CLD_KILLED) {
    status.set_message("Exited with signal " + status_code_str);
  } else {
    status.set_message("Quit with code " + status_code_str);
  }
  return response;
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
  // Redirect stdin, stdout, stderr back to the cvd client
  command.RedirectStdIO(Subprocess::StdIOChannel::kStdIn, param.in);
  command.RedirectStdIO(Subprocess::StdIOChannel::kStdOut, param.out);
  command.RedirectStdIO(Subprocess::StdIOChannel::kStdErr, param.err);

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
    const std::string& bin_file, const cvd_common::Envs& envs,
    const std::vector<std::string>& subcmd_args,
    const RequestWithStdio& request) {
  const auto host_artifacts_path = envs.at("ANDROID_HOST_OUT");
  const auto bin_path = host_artifacts_path + "/bin/" + bin_file;
  auto client_pwd = request.Message().command_request().working_directory();
  const auto home = (Contains(envs, "HOME") ? envs.at("HOME") : client_pwd);
  cvd_common::Envs envs_copy{envs};
  envs_copy["HOME"] = AbsolutePath(home);
  ConstructCommandParam construct_cmd_param{.bin_path = bin_path,
                                            .home = home,
                                            .args = subcmd_args,
                                            .envs = std::move(envs_copy),
                                            .working_dir = client_pwd,
                                            .command_name = bin_file,
                                            .in = request.In(),
                                            .out = request.Out(),
                                            .err = request.Err()};
  Command help_command = CF_EXPECT(ConstructCommand(construct_cmd_param));
  return help_command;
}

/*
 * From external/gflags/src, commit:
 *  061f68cd158fa658ec0b9b2b989ed55764870047
 *
 */
constexpr static std::array help_bool_opts{
    "help", "helpfull", "helpshort", "helppackage", "helpxml", "version"};
constexpr static std::array help_str_opts{
    "helpon",
    "helpmatch",
};

bool IsHelpSubcmd(const std::vector<std::string>& args) {
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
  ParseFlags(flags, copied_args);
  // if there was any match, some in copied_args were consumed.
  return (args.size() != copied_args.size());
}

Result<std::string> ClientAbsolutePath(const std::string& path, const uid_t uid,
                                       const std::string& client_pwd) {
  if (path.empty()) {
    return path;
  }
  auto first_char = *(path.cbegin());
  if (first_char == '/') {
    return path;
  }
  if (first_char == '~') {
    auto system_wide_user_home = CF_EXPECT(SystemWideUserHome(uid));
    auto abs_user_home = AbsolutePath(system_wide_user_home);
    CF_EXPECT(!abs_user_home.empty());
    return android::base::StringReplace(path, "~", abs_user_home, false);
  }

  // likely relative path
  CF_EXPECT(!client_pwd.empty() && client_pwd[0] == '/');
  auto prefix = (*client_pwd.rbegin() == '/') ? client_pwd : (client_pwd + "/");
  auto result_abs_path = AbsolutePath(prefix + path);
  CF_EXPECT(!result_abs_path.empty());
  return result_abs_path;
}

}  // namespace cuttlefish
