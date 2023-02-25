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

#include <sys/types.h>

#include <android-base/file.h>
#include <fruit/fruit.h>

#include "cvd_server.pb.h"

#include "common/libs/fs/shared_buf.h"
#include "common/libs/fs/shared_fd.h"
#include "common/libs/utils/result.h"
#include "host/commands/cvd/instance_manager.h"
#include "host/commands/cvd/server_command/components.h"
#include "host/commands/cvd/server_command/utils.h"
#include "host/commands/cvd/types.h"
#include "host/libs/web/build_api.h"

namespace cuttlefish {
namespace {

Result<SharedFD> LatestCvdAsFd(BuildApi& build_api) {
  static constexpr char kBuild[] = "aosp-master";
  static constexpr char kTarget[] = "aosp_cf_x86_64_phone-userdebug";
  auto latest = CF_EXPECT(build_api.LatestBuildId(kBuild, kTarget));
  DeviceBuild build{latest, kTarget};

  auto fd = SharedFD::MemfdCreate("cvd");
  CF_EXPECT(fd->IsOpen(), "MemfdCreate failed: " << fd->StrError());

  auto write = [fd](char* data, size_t size) -> bool {
    if (size == 0) {
      return true;
    }
    auto written = WriteAll(fd, data, size);
    if (written != size) {
      LOG(ERROR) << "Failed to persist data: " << fd->StrError();
      return false;
    }
    return true;
  };
  CF_EXPECT(build_api.ArtifactToCallback(build, "cvd", write));

  return fd;
}

class CvdRestartHandler : public CvdServerHandler {
 public:
  INJECT(CvdRestartHandler(BuildApi& build_api, CvdServer& server,
                           InstanceManager& instance_manager))
      : build_api_(build_api),
        server_(server),
        instance_manager_(instance_manager) {}

  Result<bool> CanHandle(const RequestWithStdio& request) const override {
    auto invocation = ParseInvocation(request.Message());
    return android::base::Basename(invocation.command) == kRestartServer;
  }

  Result<cvd::Response> Handle(const RequestWithStdio& request) override {
    CF_EXPECT(CanHandle(request));
    CF_EXPECT(request.Credentials() != std::nullopt);
    const uid_t uid = request.Credentials()->uid;
    cvd::Response response;
    response.mutable_shutdown_response();

    if (instance_manager_.HasInstanceGroups(uid)) {
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
    } else if (arguments.size() > 0 && arguments[0] == "latest") {
      new_exe = CF_EXPECT(LatestCvdAsFd(build_api_));
    } else if (arguments.size() == 0) {
      new_exe = SharedFD::Open(kServerExecPath, O_RDONLY);
      CF_EXPECT(new_exe->IsOpen(), "Failed to open \""
                                       << kServerExecPath
                                       << "\": " << new_exe->StrError());
    } else {
      return CF_ERR("Unrecognized command line");
    }
    CF_EXPECT(server_.Exec(new_exe, request.Client()));
    return CF_ERR("Should be unreachable");
  }

  Result<void> Interrupt() override { return CF_ERR("Can't interrupt"); }
  cvd_common::Args CmdList() const override { return {kRestartServer}; }
  constexpr static char kRestartServer[] = "restart-server";

 private:
  BuildApi& build_api_;
  CvdServer& server_;
  InstanceManager& instance_manager_;
};

}  // namespace

fruit::Component<fruit::Required<BuildApi, CvdServer, InstanceManager>>
CvdRestartComponent() {
  return fruit::createComponent()
      .addMultibinding<CvdServerHandler, CvdRestartHandler>();
}

}  // namespace cuttlefish
