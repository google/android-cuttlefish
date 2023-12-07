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

#include <fruit/fruit.h>

#include "cvd_server.pb.h"

#include "common/libs/fs/shared_buf.h"
#include "common/libs/fs/shared_fd.h"
#include "common/libs/utils/result.h"
#include "host/commands/cvd/instance_manager.h"

namespace cuttlefish {
namespace {

class CvdShutdownHandler : public CvdServerHandler {
 public:
  INJECT(CvdShutdownHandler(CvdServer& server,
                            InstanceManager& instance_manager))
      : server_(server), instance_manager_(instance_manager) {}

  Result<bool> CanHandle(const RequestWithStdio& request) const override {
    return request.Message().contents_case() ==
           cvd::Request::ContentsCase::kShutdownRequest;
  }

  Result<cvd::Response> Handle(const RequestWithStdio& request) override {
    CF_EXPECT(CanHandle(request));
    cvd::Response response;
    response.mutable_shutdown_response();

    if (!request.Extra()) {
      response.mutable_status()->set_code(cvd::Status::FAILED_PRECONDITION);
      response.mutable_status()->set_message(
          "Missing extra SharedFD for shutdown");
      return response;
    }

    if (request.Message().shutdown_request().clear()) {
      *response.mutable_status() =
          instance_manager_.CvdClear(request.Out(), request.Err());
      if (response.status().code() != cvd::Status::OK) {
        return response;
      }
    }

    if (instance_manager_.HasInstanceGroups()) {
      response.mutable_status()->set_code(cvd::Status::FAILED_PRECONDITION);
      response.mutable_status()->set_message(
          "Cannot shut down cvd_server while devices are being tracked. "
          "Try `cvd kill-server`.");
      return response;
    }

    // Intentionally leak the write_pipe fd so that it only closes
    // when this process fully exits.
    (*request.Extra())->UNMANAGED_Dup();

    WriteAll(request.Out(), "Stopping the cvd_server.\n");
    server_.Stop();

    response.mutable_status()->set_code(cvd::Status::OK);
    return response;
  }

  Result<void> Interrupt() override { return CF_ERR("Can't interrupt"); }

 private:
  CvdServer& server_;
  InstanceManager& instance_manager_;
};

}  // namespace

fruit::Component<fruit::Required<CvdServer, InstanceManager>>
cvdShutdownComponent() {
  return fruit::createComponent()
      .addMultibinding<CvdServerHandler, CvdShutdownHandler>();
}

}  // namespace cuttlefish
