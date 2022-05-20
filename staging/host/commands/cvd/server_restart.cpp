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

#include "host/commands/cvd/server.h"

#include <android-base/file.h>
#include <fruit/fruit.h>

#include "cvd_server.pb.h"

#include "common/libs/fs/shared_buf.h"
#include "common/libs/fs/shared_fd.h"
#include "common/libs/utils/result.h"
#include "host/commands/cvd/instance_manager.h"

namespace cuttlefish {
namespace {

class CvdRestartHandler : public CvdServerHandler {
 public:
  INJECT(CvdRestartHandler(CvdServer& server,
                           InstanceManager& instance_manager))
      : server_(server), instance_manager_(instance_manager) {}

  Result<bool> CanHandle(const RequestWithStdio& request) const override {
    auto invocation = ParseInvocation(request.Message());
    return android::base::Basename(invocation.command) == "restart-server";
  }

  Result<cvd::Response> Handle(const RequestWithStdio& request) override {
    CF_EXPECT(CanHandle(request));
    cvd::Response response;
    response.mutable_shutdown_response();

    if (instance_manager_.HasInstanceGroups()) {
      response.mutable_status()->set_code(cvd::Status::FAILED_PRECONDITION);
      response.mutable_status()->set_message(
          "Cannot restart cvd_server while devices are being tracked. "
          "Try `cvd kill-server`.");
      return response;
    }

    WriteAll(request.Out(), "Stopping the cvd_server.\n");
    server_.Stop();

    auto arguments = ParseInvocation(request.Message()).arguments;
    SharedFD new_exe;
    if (arguments.size() > 0 && arguments[0] == "match-client") {
      CF_EXPECT(request.Extra(), "Missing executable file descriptor");
      new_exe = *request.Extra();
    } else {
      constexpr char kSelf[] = "/proc/self/exe";
      new_exe = SharedFD::Open(kSelf, O_RDONLY);
      CF_EXPECT(new_exe->IsOpen(),
                "Failed to open \"" << kSelf << "\": " << new_exe->StrError());
    }
    CF_EXPECT(server_.Exec(new_exe, request.Client()));
    return CF_ERR("Should be unreachable");
  }

  Result<void> Interrupt() override { return CF_ERR("Can't interrupt"); }

 private:
  CvdServer& server_;
  InstanceManager& instance_manager_;
};

}  // namespace

fruit::Component<fruit::Required<CvdServer, InstanceManager>>
CvdRestartComponent() {
  return fruit::createComponent()
      .addMultibinding<CvdServerHandler, CvdRestartHandler>();
}

}  // namespace cuttlefish
