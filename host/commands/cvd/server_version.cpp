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

#include <build/version.h>
#include <cvd_server.pb.h>
#include <fruit/fruit.h>

#include "common/libs/utils/result.h"
#include "host/commands/cvd/server_constants.h"
#include "host/commands/cvd/types.h"
#include "host/libs/config/host_tools_version.h"

namespace cuttlefish {
namespace {

class CvdVersionHandler : public CvdServerHandler {
 public:
  INJECT(CvdVersionHandler()) = default;

  Result<bool> CanHandle(const RequestWithStdio& request) const override {
    return request.Message().contents_case() ==
           cvd::Request::ContentsCase::kVersionRequest;
  }

  Result<cvd::Response> Handle(const RequestWithStdio& request) override {
    CF_EXPECT(CanHandle(request));
    cvd::Response response;
    auto& version = *response.mutable_version_response()->mutable_version();
    version.set_major(cvd::kVersionMajor);
    version.set_minor(cvd::kVersionMinor);
    version.set_build(android::build::GetBuildNumber());
    version.set_crc32(FileCrc("/proc/self/exe"));
    response.mutable_status()->set_code(cvd::Status::OK);
    return response;
  }

  Result<void> Interrupt() override { return CF_ERR("Can't interrupt"); }

  cvd_common::Args CmdList() const override { return {"version"}; }
};

}  // namespace

fruit::Component<> cvdVersionComponent() {
  return fruit::createComponent()
      .addMultibinding<CvdServerHandler, CvdVersionHandler>();
}

}  // namespace cuttlefish
