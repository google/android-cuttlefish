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

#include <string>
#include <string_view>
#include <vector>

#include "cuttlefish/host/commands/cvd/cvd_server.pb.h"

#include "common/libs/fs/shared_fd.h"
#include "common/libs/utils/result.h"
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

struct ConstructCommandParam {
  const std::string& bin_path;
  const std::string& home;
  const std::vector<std::string>& args;
  const cvd_common::Envs& envs;
  const std::string& working_dir;
  const std::string& command_name;
  bool null_stdio;
};
Result<Command> ConstructCommand(const ConstructCommandParam& cmd_param);

// Constructs a command for cvd whatever --help or --help-related-option
Result<Command> ConstructCvdHelpCommand(const std::string& bin_file,
                                        cvd_common::Envs envs,
                                        const cvd_common::Args& _args,
                                        const RequestWithStdio& request);

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
    const ConstructNonHelpForm& request_form, const RequestWithStdio& request);

// e.g. cvd start --help, cvd stop --help
Result<bool> IsHelpSubcmd(const std::vector<std::string>& args);

// Call this when there is no instance group is running
// The function does not verify that.
Result<cvd::Response> NoGroupResponse(const RequestWithStdio& request);

// Call this when there is more than one group, which the selector flags are
// not sufficients to choose one from. The function does not verify that.
Result<cvd::Response> NoTTYResponse(const RequestWithStdio& request);

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

Result<cvd::Response> WriteToFd(SharedFD fd, const std::string& output);

}  // namespace cuttlefish
