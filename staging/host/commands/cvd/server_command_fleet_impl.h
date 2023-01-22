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

#include <map>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include <fruit/fruit.h>
#include "cvd_server.pb.h"

#include "common/libs/fs/shared_fd.h"
#include "common/libs/utils/result.h"
#include "common/libs/utils/subprocess.h"
#include "host/commands/cvd/instance_manager.h"
#include "host/commands/cvd/server_command/server_handler.h"
#include "host/commands/cvd/server_command/subprocess_waiter.h"
#include "host/commands/cvd/types.h"

namespace cuttlefish {
namespace cvd_cmd_impl {

class CvdFleetCommandHandler : public CvdServerHandler {
 public:
  INJECT(CvdFleetCommandHandler(InstanceManager& instance_manager,
                                SubprocessWaiter& subprocess_waiter))
      : instance_manager_(instance_manager),
        subprocess_waiter_(subprocess_waiter) {}

  Result<bool> CanHandle(const RequestWithStdio& request) const;
  Result<cvd::Response> Handle(const RequestWithStdio& request) override;
  Result<void> Interrupt() override;
  cvd_common::Args CmdList() const override { return {kFleetSubcmd}; }

 private:
  InstanceManager& instance_manager_;
  SubprocessWaiter& subprocess_waiter_;
  std::mutex interruptible_;
  bool interrupted_ = false;

  static constexpr char kFleetSubcmd[] = "fleet";
  Result<cvd::Status> HandleCvdFleet(const uid_t uid, const SharedFD& out,
                                     const SharedFD& err,
                                     const cvd_common::Args& cmd_args) const;
  Result<cvd::Status> CvdFleetHelp(const SharedFD& out) const;
  bool IsHelp(const cvd_common::Args& cmd_args) const;
};

}  // namespace cvd_cmd_impl
}  // namespace cuttlefish
