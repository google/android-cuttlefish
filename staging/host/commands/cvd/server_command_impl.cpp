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

#include "common/libs/utils/contains.h"
#include "common/libs/utils/files.h"
#include "host/commands/cvd/instance_manager.h"
#include "host/commands/cvd/server.h"
#include "host/libs/config/cuttlefish_config.h"

namespace cuttlefish {
namespace cvd_cmd_impl {
namespace {

Envs ConvertMap(
    const google::protobuf::Map<std::string, std::string>& proto_map) {
  Envs envs;
  for (const auto& entry : proto_map) {
    envs[entry.first] = entry.second;
  }
  return envs;
}

}  // namespace

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
  Envs envs = ConvertMap(request.Message().command_request().env());
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

Result<Command> ConstructCommand(const std::string& bin_path,
                                 const std::string& home,
                                 const std::vector<std::string>& args,
                                 const Envs& envs,
                                 const std::string& working_dir,
                                 const std::string& command_name, SharedFD in,
                                 SharedFD out, SharedFD err) {
  Command command(command_name);
  command.SetExecutable(bin_path);
  for (const std::string& arg : args) {
    command.AddParameter(arg);
  }
  // Set CuttlefishConfig path based on assembly dir,
  // used by subcommands when locating the CuttlefishConfig.
  if (envs.count(cuttlefish::kCuttlefishConfigEnvVarName) == 0) {
    auto config_path = InstanceManager::GetCuttlefishConfigPath(home);
    if (config_path.ok()) {
      command.AddEnvironmentVariable(cuttlefish::kCuttlefishConfigEnvVarName,
                                     *config_path);
    }
  }
  for (auto& it : envs) {
    command.UnsetFromEnvironment(it.first);
    command.AddEnvironmentVariable(it.first, it.second);
  }
  // Redirect stdin, stdout, stderr back to the cvd client
  command.RedirectStdIO(Subprocess::StdIOChannel::kStdIn, std::move(in));
  command.RedirectStdIO(Subprocess::StdIOChannel::kStdOut, std::move(out));
  command.RedirectStdIO(Subprocess::StdIOChannel::kStdErr, std::move(err));
  if (!working_dir.empty()) {
    auto fd = SharedFD::Open(working_dir, O_RDONLY | O_PATH | O_DIRECTORY);
    CF_EXPECT(fd->IsOpen(),
              "Couldn't open \"" << working_dir << "\": " << fd->StrError());
    command.SetWorkingDirectory(fd);
  }
  return {std::move(command)};
}

}  // namespace cvd_cmd_impl
}  // namespace cuttlefish
