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
#include "host/commands/cvd/types.h"

namespace cuttlefish {

struct CommandInvocation {
  std::string command;
  std::vector<std::string> arguments;
};

CommandInvocation ParseInvocation(const cvd::Request& request);

cuttlefish::cvd::Response ResponseFromSiginfo(siginfo_t infop);

struct PreconditionVerification {
  bool is_ok;
  std::string error_message;
};
PreconditionVerification VerifyPrecondition(const RequestWithStdio& request);

struct ConstructCommandParam {
  const std::string& bin_path;
  const std::string& home;
  const std::vector<std::string>& args;
  const cvd_common::Envs& envs;
  const std::string& working_dir;
  const std::string& command_name;
  SharedFD in;
  SharedFD out;
  SharedFD err;
};
Result<Command> ConstructCommand(const ConstructCommandParam& cmd_param);

// Constructs a command for cvd whatever --help or --help-related-option
Result<Command> ConstructCvdHelpCommand(
    const std::string& bin_file, const cvd_common::Envs& envs,
    const std::vector<std::string>& subcmd_args,
    const RequestWithStdio& request);

// e.g. cvd start --help, cvd stop --help
bool IsHelpSubcmd(const std::vector<std::string>& args);

/**
 * Calculates absolute path based on the client's environment
 *
 * If the client sent a relative path like "bin/foo", it is relative
 * to the client's working directory, not to the server's.
 * Likewise, if the client sent a path that starts with ~, we should
 * replace ~ with the client user's home, not the server user's.
 */
Result<std::string> ClientAbsolutePath(const std::string& path, const uid_t uid,
                                       const std::string& client_pwd);

}  // namespace cuttlefish
