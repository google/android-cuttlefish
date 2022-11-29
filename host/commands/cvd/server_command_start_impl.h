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
  Result<void> UpdateInstanceDatabase(
      const uid_t uid, const selector::GroupCreationInfo& group_creation_info);
  Result<void> FireCommand(Command&& command, const bool wait);
  bool HasHelpOpts(const std::vector<std::string>& args) const;

  Result<Command> ConstructCvdNonHelpCommand(
      const std::string& bin_file,
      const selector::GroupCreationInfo& group_info,
      const RequestWithStdio& request);

  // call this only if !is_help
  Result<selector::GroupCreationInfo> GetGroupCreationInfo(
      const std::string& subcmd, const std::vector<std::string>& subcmd_args,
      const Envs& envs, const RequestWithStdio& request);

  InstanceManager& instance_manager_;
  SubprocessWaiter& subprocess_waiter_;
  std::mutex interruptible_;
  bool interrupted_ = false;

  static constexpr char kStartBin[] = "cvd_internal_start";
  static const std::map<std::string, std::string> command_to_binary_map_;
};

}  // namespace cvd_cmd_impl
}  // namespace cuttlefish
