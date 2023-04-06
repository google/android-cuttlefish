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
#include "host/commands/cvd/server_command/server_handler.h"
#include "host/commands/cvd/server_command/utils.h"
#include "host/commands/cvd/types.h"

namespace cuttlefish {

class TryAcloudCommand : public CvdServerHandler {
 public:
  INJECT(TryAcloudCommand(ConvertAcloudCreateCommand& converter,
                          ANNOTATED(AcloudTranslatorOptOut,
                                    const std::atomic<bool>&) optout))
      : converter_(converter), optout_(optout) {}
  ~TryAcloudCommand() = default;

  Result<bool> CanHandle(const RequestWithStdio& request) const override {
    auto invocation = ParseInvocation(request.Message());
    return invocation.command == "try-acloud";
  }

  cvd_common::Args CmdList() const override { return {"try-acloud"}; }

  Result<cvd::Response> Handle(const RequestWithStdio& request) override {
    CF_EXPECT(CanHandle(request));
    CF_EXPECT(IsSubOperationSupported(request));
    CF_EXPECT(converter_.Convert(request));
    // currently, optout/optin feature only works in local instance
    // remote instance still uses legacy python acloud
    CF_EXPECT(!optout_);
    cvd::Response response;
    response.mutable_command_response();
    return response;
  }
  Result<void> Interrupt() override { return CF_ERR("Can't be interrupted."); }

 private:
  ConvertAcloudCreateCommand& converter_;
  const std::atomic<bool>& optout_;
};

fruit::Component<fruit::Required<
    ConvertAcloudCreateCommand,
    fruit::Annotated<AcloudTranslatorOptOut, std::atomic<bool>>>>
TryAcloudCommandComponent() {
  return fruit::createComponent()
      .addMultibinding<CvdServerHandler, TryAcloudCommand>();
}

}  // namespace cuttlefish
