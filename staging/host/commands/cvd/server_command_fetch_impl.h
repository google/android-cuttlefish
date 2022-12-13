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
#include "host/commands/cvd/server.h"
#include "host/commands/cvd/server_command_impl.h"
#include "host/commands/cvd/server_command_subprocess_waiter.h"
#include "host/commands/cvd/types.h"

namespace cuttlefish {
namespace cvd_cmd_impl {

class CvdFetchHandler : public CvdServerHandler {
 public:
  INJECT(CvdFetchHandler(SubprocessWaiter& subprocess_waiter))
      : subprocess_waiter_(subprocess_waiter),
        fetch_cmd_list_{std::vector<std::string>{"fetch", "fetch_cvd"}} {}

  Result<bool> CanHandle(const RequestWithStdio& request) const override;
  Result<cvd::Response> Handle(const RequestWithStdio& request) override;
  Result<void> Interrupt() override;
  cvd_common::Args CmdList() const override { return fetch_cmd_list_; }

 private:
  SubprocessWaiter& subprocess_waiter_;
  std::mutex interruptible_;
  bool interrupted_ = false;
  std::vector<std::string> fetch_cmd_list_;
};

}  // namespace cvd_cmd_impl
}  // namespace cuttlefish
