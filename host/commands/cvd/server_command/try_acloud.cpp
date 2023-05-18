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

#include "host/commands/cvd/server_command/try_acloud.h"

#include <mutex>

#include <fruit/fruit.h>

#include "common/libs/utils/result.h"
#include "cvd_server.pb.h"
#include "host/commands/cvd/acloud/converter.h"
#include "host/commands/cvd/server_command/server_handler.h"
#include "host/commands/cvd/server_command/subprocess_waiter.h"
#include "host/commands/cvd/server_command/utils.h"
#include "host/commands/cvd/types.h"

namespace cuttlefish {

class TryAcloudCommand : public CvdServerHandler {
 public:
  INJECT(TryAcloudCommand(ANNOTATED(AcloudTranslatorOptOut,
                                    const std::atomic<bool>&) optout))
      : optout_(optout) {}
  ~TryAcloudCommand() = default;

  Result<bool> CanHandle(const RequestWithStdio& request) const override {
    auto invocation = ParseInvocation(request.Message());
    return invocation.command == "try-acloud";
  }

  cvd_common::Args CmdList() const override { return {"try-acloud"}; }

  Result<cvd::Response> Handle(const RequestWithStdio& request) override {
    std::unique_lock interrupt_lock(interrupt_mutex_);
    CF_EXPECT(!interrupted_, "Interrupted");
    CF_EXPECT(CanHandle(request));
    CF_EXPECT(IsSubOperationSupported(request));
    auto converted = CF_EXPECT(
        acloud_impl::ConvertAcloudCreate(request, waiter_, interrupt_lock));
    if (converted.interrupt_lock_released) {
      interrupt_lock.lock();
    }
    // currently, optout/optin feature only works in local instance
    // remote instance still uses legacy python acloud
    CF_EXPECT(!optout_);
    cvd::Response response;
    response.mutable_command_response();
    return response;
  }
  Result<void> Interrupt() override {
    std::lock_guard interrupt_lock(interrupt_mutex_);
    interrupted_ = true;
    waiter_.Interrupt();
    return {};
  }

 private:
  std::mutex interrupt_mutex_;
  bool interrupted_ = false;
  SubprocessWaiter waiter_;
  const std::atomic<bool>& optout_;
};

fruit::Component<fruit::Required<
    fruit::Annotated<AcloudTranslatorOptOut, std::atomic<bool>>>>
TryAcloudCommandComponent() {
  return fruit::createComponent()
      .addMultibinding<CvdServerHandler, TryAcloudCommand>();
}

}  // namespace cuttlefish
