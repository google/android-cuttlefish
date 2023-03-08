/*
 * Copyright (C) 2023 The Android Open Source Project
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

#include "host/commands/cvd/server_command/cmd_list.h"

#include <mutex>
#include <vector>

#include <android-base/strings.h>

#include "common/libs/fs/shared_buf.h"
#include "common/libs/utils/json.h"
#include "host/commands/cvd/server_command/server_handler.h"
#include "host/commands/cvd/server_command/utils.h"
#include "host/commands/cvd/types.h"

namespace cuttlefish {

class CvdCmdlistHandler : public CvdServerHandler {
 public:
  INJECT(CvdCmdlistHandler(CommandSequenceExecutor& executor))
      : executor_(executor) {}

  Result<bool> CanHandle(const RequestWithStdio& request) const override {
    auto invocation = ParseInvocation(request.Message());
    return (invocation.command == "cmd-list");
  }

  Result<cvd::Response> Handle(const RequestWithStdio& request) override {
    std::lock_guard interrupt_lock(interruptible_);
    CF_EXPECT(!interrupted_, "Interrupted");

    cvd::Response response;
    response.mutable_command_response();  // Sets oneof member
    response.mutable_status()->set_code(cvd::Status::OK);

    CF_EXPECT(CanHandle(request));

    auto [subcmd, subcmd_args] = ParseInvocation(request.Message());
    const auto subcmds = executor_.CmdList();

    std::vector<std::string> subcmds_vec{subcmds.begin(), subcmds.end()};
    const auto subcmds_str = android::base::Join(subcmds_vec, ",");
    Json::Value subcmd_info;
    subcmd_info["subcmd"] = subcmds_str;
    WriteAll(request.Out(), subcmd_info.toStyledString());
    return response;
  }

  Result<void> Interrupt() override {
    std::scoped_lock interrupt_lock(interruptible_);
    interrupted_ = true;
    CF_EXPECT(executor_.Interrupt());
    return {};
  }

  // not intended to be used by the user
  cvd_common::Args CmdList() const override { return {}; }

 private:
  std::mutex interruptible_;
  bool interrupted_ = false;
  CommandSequenceExecutor& executor_;
};

fruit::Component<fruit::Required<CommandSequenceExecutor>>
CvdCmdlistComponent() {
  return fruit::createComponent()
      .addMultibinding<CvdServerHandler, CvdCmdlistHandler>();
}

}  // namespace cuttlefish
