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

#include "host/commands/cvd/server_command/fleet.h"

#include <sys/types.h>

#include <mutex>

#include <android-base/file.h>

#include "common/libs/fs/shared_buf.h"
#include "common/libs/fs/shared_fd.h"
#include "common/libs/utils/contains.h"
#include "common/libs/utils/files.h"
#include "common/libs/utils/result.h"
#include "host/commands/cvd/server_command/server_handler.h"
#include "host/commands/cvd/server_command/utils.h"
#include "host/commands/cvd/types.h"
#include "host/libs/config/cuttlefish_config.h"

namespace cuttlefish {

class CvdFleetCommandHandler : public CvdServerHandler {
 public:
  INJECT(CvdFleetCommandHandler(InstanceManager& instance_manager,
                                SubprocessWaiter& subprocess_waiter))
      : instance_manager_(instance_manager),
        subprocess_waiter_(subprocess_waiter) {}

  Result<bool> CanHandle(const RequestWithStdio& request) const;
  Result<cvd::Response> Handle(const RequestWithStdio& request) override;
  Result<void> Interrupt() override;
  cvd_common::Args CmdList() const override { return {kFleetSubcmd}; }

 private:
  InstanceManager& instance_manager_;
  SubprocessWaiter& subprocess_waiter_;
  std::mutex interruptible_;
  bool interrupted_ = false;

  static constexpr char kFleetSubcmd[] = "fleet";
  Result<cvd::Status> HandleCvdFleet(const uid_t uid, const SharedFD& out,
                                     const SharedFD& err,
                                     const cvd_common::Args& cmd_args) const;
  Result<cvd::Status> CvdFleetHelp(const SharedFD& out) const;
  bool IsHelp(const cvd_common::Args& cmd_args) const;
};

Result<bool> CvdFleetCommandHandler::CanHandle(
    const RequestWithStdio& request) const {
  auto invocation = ParseInvocation(request.Message());
  return invocation.command == kFleetSubcmd;
}

Result<void> CvdFleetCommandHandler::Interrupt() {
  std::scoped_lock interrupt_lock(interruptible_);
  interrupted_ = true;
  CF_EXPECT(subprocess_waiter_.Interrupt());
  return {};
}

Result<cvd::Response> CvdFleetCommandHandler::Handle(
    const RequestWithStdio& request) {
  std::unique_lock interrupt_lock(interruptible_);
  if (interrupted_) {
    return CF_ERR("Interrupted");
  }
  CF_EXPECT(CanHandle(request));
  CF_EXPECT(request.Credentials() != std::nullopt);
  const uid_t uid = request.Credentials()->uid;

  cvd::Response response;
  response.mutable_command_response();

  auto [sub_cmd, args] = ParseInvocation(request.Message());
  auto envs =
      cvd_common::ConvertToEnvs(request.Message().command_request().env());
  if (!IsHelp(args)) {
    CF_EXPECT(Contains(envs, "ANDROID_HOST_OUT") &&
              DirectoryExists(envs.at("ANDROID_HOST_OUT")));
  }

  *response.mutable_status() =
      CF_EXPECT(HandleCvdFleet(uid, request.Out(), request.Err(), args));

  return response;
}

Result<cvd::Status> CvdFleetCommandHandler::HandleCvdFleet(
    const uid_t uid, const SharedFD& out, const SharedFD& err,
    const cvd_common::Args& cmd_args) const {
  if (IsHelp(cmd_args)) {
    auto status = CF_EXPECT(CvdFleetHelp(out));
    return status;
  }
  auto status = CF_EXPECT(instance_manager_.CvdFleet(uid, out, err, cmd_args));
  return status;
}

bool CvdFleetCommandHandler::IsHelp(const cvd_common::Args& args) const {
  for (const auto& arg : args) {
    if (arg == "--help" || arg == "-help") {
      return true;
    }
  }
  return false;
}

Result<cvd::Status> CvdFleetCommandHandler::CvdFleetHelp(
    const SharedFD& out) const {
  WriteAll(out, "Simply run \"cvd fleet\" as it has no other flags.\n");
  WriteAll(out, "\n");
  WriteAll(out, "\"cvd fleet\" will:\n");
  WriteAll(out,
           "      1. tell whether the devices (i.e. \"run_cvd\" processes) are "
           "active.\n");
  WriteAll(out,
           "      2. optionally list the active devices with information.\n");
  cvd::Status status;
  status.set_code(cvd::Status::OK);
  return status;
}

fruit::Component<fruit::Required<InstanceManager, SubprocessWaiter>>
cvdFleetCommandComponent() {
  return fruit::createComponent()
      .addMultibinding<CvdServerHandler, CvdFleetCommandHandler>();
}

}  // namespace cuttlefish
