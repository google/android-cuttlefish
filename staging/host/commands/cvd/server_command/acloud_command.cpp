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

#include "host/commands/cvd/server_command/acloud_command.h"

#include <atomic>
#include <mutex>

#include <android-base/file.h>
#include <android-base/strings.h>
#include <fruit/fruit.h>

#include "common/libs/utils/result.h"
#include "cvd_server.pb.h"
#include "host/commands/cvd/instance_lock.h"
#include "host/commands/cvd/server_command/acloud_common.h"
#include "host/commands/cvd/server_command/server_handler.h"
#include "host/commands/cvd/server_command/utils.h"
#include "host/commands/cvd/types.h"

namespace cuttlefish {

class AcloudCommand : public CvdServerHandler {
 public:
  INJECT(AcloudCommand(CommandSequenceExecutor& executor,
                       ConvertAcloudCreateCommand& converter))
      : executor_(executor), converter_(converter) {}
  ~AcloudCommand() = default;

  Result<bool> CanHandle(const RequestWithStdio& request) const override {
    auto invocation = ParseInvocation(request.Message());
    if (invocation.arguments.size() >= 2) {
      if (invocation.command == "acloud" &&
          invocation.arguments[0] == "translator") {
        return false;
      }
    }
    return invocation.command == "acloud";
  }

  cvd_common::Args CmdList() const override { return {"acloud"}; }

  Result<cvd::Response> Handle(const RequestWithStdio& request) override {
    std::unique_lock interrupt_lock(interrupt_mutex_);
    if (interrupted_) {
      return CF_ERR("Interrupted");
    }
    CF_EXPECT(CanHandle(request));
    CF_EXPECT(IsSubOperationSupported(request));
    auto converted = CF_EXPECT(converter_.Convert(request));
    interrupt_lock.unlock();
    CF_EXPECT(executor_.Execute(converted.requests, request.Err()));

    CF_EXPECT(converted.lock.Status(InUseState::kInUse));

    if (converter_.FetchCommandString() != "") {
      // has cvd fetch command, update the fetch cvd command file
      using android::base::WriteStringToFile;
      CF_EXPECT(WriteStringToFile(converter_.FetchCommandString(),
                                  converter_.FetchCvdArgsFile()),
                true);
    }

    cvd::Response response;
    response.mutable_command_response();
    return response;
  }
  Result<void> Interrupt() override {
    std::scoped_lock interrupt_lock(interrupt_mutex_);
    interrupted_ = true;
    CF_EXPECT(executor_.Interrupt());
    return {};
  }

 private:
  CommandSequenceExecutor& executor_;
  ConvertAcloudCreateCommand& converter_;

  std::mutex interrupt_mutex_;
  bool interrupted_ = false;
};

fruit::Component<
    fruit::Required<CommandSequenceExecutor, ConvertAcloudCreateCommand>>
AcloudCommandComponent() {
  return fruit::createComponent()
      .addMultibinding<CvdServerHandler, AcloudCommand>();
}

}  // namespace cuttlefish
