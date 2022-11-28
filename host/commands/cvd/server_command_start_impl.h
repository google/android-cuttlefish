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

#include <array>
#include <map>
#include <mutex>
#include <string>

#include <fruit/fruit.h>

#include "cvd_server.pb.h"

#include "common/libs/utils/result.h"
#include "common/libs/utils/subprocess.h"
#include "host/commands/cvd/instance_manager.h"
#include "host/commands/cvd/server.h"
#include "host/commands/cvd/server_command_impl.h"
#include "host/commands/cvd/server_command_subprocess_waiter.h"

namespace cuttlefish {
namespace cvd_cmd_impl {

class CvdStartCommandHandler : public CvdServerHandler {
 public:
  INJECT(CvdStartCommandHandler(InstanceManager& instance_manager,
                                SubprocessWaiter& subprocess_waiter))
      : instance_manager_(instance_manager),
        subprocess_waiter_(subprocess_waiter) {}

  Result<bool> CanHandle(const RequestWithStdio& request) const;
  Result<cvd::Response> Handle(const RequestWithStdio& request) override;
  Result<void> Interrupt() override;

 private:
  /*
   * Update instance database
   *
   * return false if the instance database wasn't expected to be set:
   *  e.g. cvd start --help
   *
   * return CF_ERR if anything fails unexpectedly (e.g. HOME directory is taken)
   */
  Result<bool> UpdateInstanceDatabase(
      const CommandInvocationInfo& invocation_info);
  Result<std::string> MakeBinPathFromDatabase(
      const CommandInvocationInfo& invocation_info) const;
  Result<void> FireCommand(Command&& command, const bool wait);
  bool HasHelpOpts(const std::vector<std::string>& args) const;
  struct PreconditionVerification {
    bool is_ok;
    std::string error_message;
  };
  PreconditionVerification VerifyPrecondition(
      const RequestWithStdio& request) const;

  InstanceManager& instance_manager_;
  SubprocessWaiter& subprocess_waiter_;
  std::mutex interruptible_;
  bool interrupted_ = false;

  static constexpr char kStartBin[] = "cvd_internal_start";
  static const std::map<std::string, std::string> command_to_binary_map_;

  /*
   * From external/gflags/src, commit:
   *  061f68cd158fa658ec0b9b2b989ed55764870047
   *
   */
  constexpr static std::array help_bool_opts_{
      "help", "helpfull", "helpshort", "helppackage", "helpxml", "version"};
  constexpr static std::array help_str_opts_{
      "helpon",
      "helpmatch",
  };
};

}  // namespace cvd_cmd_impl
}  // namespace cuttlefish
