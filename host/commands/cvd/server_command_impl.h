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

#include <sys/types.h>

#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "cvd_server.pb.h"

#include "common/libs/utils/subprocess.h"
#include "host/commands/cvd/server_client.h"

namespace cuttlefish {
namespace cvd_cmd_impl {

// methods shared by CvdCommandHandler and CvdStartCommandHandler

using Envs = std::unordered_map<std::string, std::string>;
struct CommandInvocationInfo {
  std::string command;
  std::string bin;
  std::string home;
  std::string host_artifacts_path;
  uid_t uid;
  std::vector<std::string> args;
  Envs envs;
};

cuttlefish::cvd::Response ResponseFromSiginfo(siginfo_t infop);

std::optional<CommandInvocationInfo> ExtractInfo(
    const std::map<std::string, std::string>& command_to_binary_map,
    const RequestWithStdio& request);

Result<Command> ConstructCommand(const std::string& bin_path,
                                 const std::string& home,
                                 const std::vector<std::string>& args,
                                 const Envs& envs,
                                 const std::string& working_dir,
                                 const std::string& command_name, SharedFD in,
                                 SharedFD out, SharedFD err);

}  // namespace cvd_cmd_impl
}  // namespace cuttlefish
