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

#include "host/commands/cvd/server_command_impl.h"

#include <array>

#include "common/libs/utils/contains.h"
#include "common/libs/utils/files.h"
#include "common/libs/utils/flag_parser.h"
#include "host/commands/cvd/instance_manager.h"
#include "host/commands/cvd/server.h"
#include "host/libs/config/cuttlefish_config.h"

namespace cuttlefish {
namespace cvd_cmd_impl {

std::vector<std::string> ConvertProtoArguments(
    const google::protobuf::RepeatedPtrField<std::string>& proto_args) {
  std::vector<std::string> args;
  for (const auto& proto_arg : proto_args) {
    args.emplace_back(proto_arg);
  }
  return args;
}

Envs ConvertProtoMap(
    const google::protobuf::Map<std::string, std::string>& proto_map) {
  Envs envs;
  for (const auto& entry : proto_map) {
    envs[entry.first] = entry.second;
  }
  return envs;
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

std::optional<CommandInvocationInfo> ExtractInfo(
    const std::map<std::string, std::string>& command_to_binary_map,
    const RequestWithStdio& request) {
  auto result_opt = request.Credentials();
  if (!result_opt) {
    return std::nullopt;
  }
  const uid_t uid = result_opt->uid;

  auto [command, args] = ParseInvocation(request.Message());
  if (!Contains(command_to_binary_map, command)) {
    return std::nullopt;
  }
  const auto& bin = command_to_binary_map.at(command);
  Envs envs = ConvertProtoMap(request.Message().command_request().env());
  std::string home =
      Contains(envs, "HOME") ? envs.at("HOME") : StringFromEnv("HOME", ".");
  if (!Contains(envs, "ANDROID_HOST_OUT") ||
      !DirectoryExists(envs.at("ANDROID_HOST_OUT"))) {
    return std::nullopt;
  }
  const auto host_artifacts_path = envs.at("ANDROID_HOST_OUT");
  // TODO(kwstephenkim): eat --base_instance_num and --num_instances
  // or --instance_nums, and override/delete kCuttlefishInstanceEnvVarName
  // in envs
  CommandInvocationInfo result = {.command = command,
                                  .bin = bin,
                                  .home = home,
                                  .host_artifacts_path = host_artifacts_path,
                                  .uid = uid,
                                  .args = args,
                                  .envs = envs};
  result.envs["HOME"] = home;
  return {result};
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
  command.RedirectStdIO(Subprocess::StdIOChannel::kStdIn, std::move(param.in));
  command.RedirectStdIO(Subprocess::StdIOChannel::kStdOut,
                        std::move(param.out));
  command.RedirectStdIO(Subprocess::StdIOChannel::kStdErr,
                        std::move(param.err));
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
    const std::string& bin_file, const Envs& envs,
    const std::vector<std::string>& subcmd_args,
    const RequestWithStdio& request) {
  const auto host_artifacts_path = envs.at("ANDROID_HOST_OUT");
  const auto bin_path = host_artifacts_path + "/bin/" + bin_file;
  auto client_pwd = request.Message().command_request().working_directory();
  const auto home = (Contains(envs, "HOME") ? envs.at("HOME") : client_pwd);
  Envs envs_copy{envs};
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

}  // namespace cvd_cmd_impl
}  // namespace cuttlefish
