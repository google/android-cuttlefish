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

#include "host/commands/cvd/server_command/fleet.h"

#include <iostream>
#include <sstream>

#include "common/libs/fs/shared_buf.h"
#include "host/commands/cvd/server_command/server_handler.h"
#include "host/commands/cvd/server_command/utils.h"

namespace cuttlefish {

/*
 * Prints out help message for cvd reset
 *
 * cvd reset is a feature implemented by the client. However, the user may run
 * cvd help reset. The cvd help parsing will be done on the server side, and
 * forwarded to the cvd help handler. The cvd help handler again will forward
 * it to supposedly cvd reset handler. The cvd reset handler will only receive
 * "cvd reset --help."
 *
 * For, say, "cvd reset" or even "cvd reset --help"," the parsing will be done
 * on the client side, and handled by the client.
 *
 */
class CvdResetCommandHandler : public CvdServerHandler {
 public:
  INJECT(CvdResetCommandHandler()) {}

  Result<bool> CanHandle(const RequestWithStdio& request) const {
    auto invocation = ParseInvocation(request.Message());
    return invocation.command == kResetSubcmd;
  }
  Result<cvd::Response> Handle(const RequestWithStdio& request) override {
    CF_EXPECT(CanHandle(request));
    cvd::Response response;
    response.mutable_command_response();
    response.mutable_status()->set_code(cvd::Status::OK);
    std::stringstream guide_message;
    guide_message << "\"cvd reset\" is implemented on the client side."
                  << " Try:" << std::endl;
    guide_message << "  cvd reset --help" << std::endl;
    const auto guide_message_str = guide_message.str();
    CF_EXPECT_EQ(WriteAll(request.Err(), guide_message_str),
                 guide_message_str.size());
    return response;
  }
  Result<void> Interrupt() override { return CF_ERR("Can't interrupt"); }
  cvd_common::Args CmdList() const override { return {kResetSubcmd}; }

 private:
  static constexpr char kResetSubcmd[] = "reset";
};

fruit::Component<> CvdResetComponent() {
  return fruit::createComponent()
      .addMultibinding<CvdServerHandler, CvdResetCommandHandler>();
}

}  // namespace cuttlefish
