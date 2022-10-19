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

#include <mutex>

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

class CvdCommandHandler : public CvdServerHandler {
 public:
  INJECT(CvdCommandHandler(InstanceManager& instance_manager,
                           SubprocessWaiter& subprocess_waiter))
      : instance_manager_(instance_manager),
        subprocess_waiter_(subprocess_waiter) {}

  Result<bool> CanHandle(const RequestWithStdio& request) const;
  Result<cvd::Response> Handle(const RequestWithStdio& request) override;
  Result<void> Interrupt() override;

 private:
  Result<cvd::Status> HandleCvdFleet(const RequestWithStdio& request,
                                     const std::vector<std::string>& args,
                                     const std::string& host_artifacts_path);
  InstanceManager& instance_manager_;
  SubprocessWaiter& subprocess_waiter_;
  std::mutex interruptible_;
  bool interrupted_ = false;

  static constexpr char kHostBugreportBin[] = "cvd_internal_host_bugreport";
  static constexpr char kDisplayBin[] = "cvd_internal_display";
  static constexpr char kLnBin[] = "ln";
  static constexpr char kMkdirBin[] = "mkdir";

  static constexpr char kClearBin[] =
      "clear_placeholder";  // Unused, runs CvdClear()
  static constexpr char kFleetBin[] =
      "fleet_placeholder";  // Unused, runs CvdFleet()

  static const std::map<std::string, std::string> command_to_binary_map_;
};

}  // namespace cvd_cmd_impl
}  // namespace cuttlefish
