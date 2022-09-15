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

#include "common/libs/utils/files.h"
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
  auto [command, args] = ParseInvocation(request.Message());
  auto subcommand_bin = command_to_binary_map.find(command);
  if (subcommand_bin == command_to_binary_map.end()) {
    return std::nullopt;
  }
  auto bin = subcommand_bin->second;
  Envs envs = ConvertMap(request.Message().command_request().env());
  auto request_home = envs.find("HOME");
  std::string home = request_home != envs.end() ? request_home->second
                                                : StringFromEnv("HOME", ".");
  auto host_out_itr = envs.find("ANDROID_HOST_OUT");
  if (host_out_itr == envs.end() && !DirectoryExists(host_out_itr->second)) {
    return std::nullopt;
  }
  const auto host_artifacts_path = host_out_itr->second;
  // TODO(kwstephenkim): eat --base_instance_num and --num_instances
  // or --instance_nums, and override/delete CUTTLEFISH_INSTANCE in envs
  CommandInvocationInfo result = {.command = command,
                                  .bin = bin,
                                  .home = home,
                                  .host_artifacts_path = host_artifacts_path,
                                  .args = args,
                                  .envs = envs};
  return {result};
}

Result<Command> ConstructCommand(const std::string& bin_path,
                                 const std::string& home,
                                 const std::vector<std::string>& args,
                                 const Envs& envs,
                                 const std::string& working_dir,
                                 const std::string& command_name, SharedFD in,
                                 SharedFD out, SharedFD err) {
  Command command(bin_path);
  command.SetName(command_name);
  for (const std::string& arg : args) {
    command.AddParameter(arg);
  }
  // Set CuttlefishConfig path based on assembly dir,
  // used by subcommands when locating the CuttlefishConfig.
  if (envs.count(kCuttlefishConfigEnvVarName) == 0) {
    auto config_path = GetCuttlefishConfigPath(home);
    if (config_path.ok()) {
      command.AddEnvironmentVariable(kCuttlefishConfigEnvVarName, *config_path);
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
