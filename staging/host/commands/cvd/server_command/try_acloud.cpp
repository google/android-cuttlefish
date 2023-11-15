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

#include "common/libs/utils/result.h"
#include "cvd_server.pb.h"
#include "host/commands/cvd/acloud/config.h"
#include "host/commands/cvd/acloud/converter.h"
#include "host/commands/cvd/acloud/create_converter_parser.h"
#include "host/commands/cvd/server_command/server_handler.h"
#include "host/commands/cvd/server_command/subprocess_waiter.h"
#include "host/commands/cvd/server_command/utils.h"
#include "host/commands/cvd/types.h"

namespace cuttlefish {

class TryAcloudCommand : public CvdServerHandler {
 public:
  TryAcloudCommand(const std::atomic<bool>& optout) : optout_(optout) {}
  ~TryAcloudCommand() = default;

  Result<bool> CanHandle(const RequestWithStdio& request) const override {
    auto invocation = ParseInvocation(request.Message());
    return invocation.command == "try-acloud";
  }

  cvd_common::Args CmdList() const override { return {"try-acloud"}; }

  /**
   * The `try-acloud` command verifies whether an original `acloud CLI` command
   * could be satisfied using either:
   *
   * - `cvd` for local instance management, determined by flag
   * `--local-instance`.
   *
   * - Or `cvdr` for remote instance management.
   *
   * If the test fails, the command will be handed to the `python acloud CLI`.
   *
   */
  Result<cvd::Response> Handle(const RequestWithStdio& request) override {
    auto res = VerifyWithCvdRemote(request);
    return res.ok() ? res : VerifyWithCvd(request);
  }

  Result<void> Interrupt() override {
    std::lock_guard interrupt_lock(interrupt_mutex_);
    interrupted_ = true;
    waiter_.Interrupt();
    return {};
  }

 private:
  Result<cvd::Response> VerifyWithCvd(const RequestWithStdio& request);
  Result<cvd::Response> VerifyWithCvdRemote(const RequestWithStdio& request);

  std::mutex interrupt_mutex_;
  bool interrupted_ = false;
  SubprocessWaiter waiter_;
  const std::atomic<bool>& optout_;
};

Result<cvd::Response> TryAcloudCommand::VerifyWithCvd(
    const RequestWithStdio& request) {
  std::unique_lock interrupt_lock(interrupt_mutex_);
  bool lock_released = false;
  CF_EXPECT(!interrupted_, "Interrupted");
  CF_EXPECT(CanHandle(request));
  CF_EXPECT(IsSubOperationSupported(request));
  auto cb_unlock = [&lock_released, &interrupt_lock](void) -> Result<void> {
    if (!lock_released) {
      interrupt_lock.unlock();
      lock_released = true;
    }
    return {};
  };
  auto cb_lock = [&lock_released, &interrupt_lock](void) -> Result<void> {
    if (lock_released) {
      interrupt_lock.lock();
      lock_released = true;
    }
    return {};
  };
  // ConvertAcloudCreate converts acloud to cvd commands.
  // The input parameters waiter_, cb_unlock, cb_lock are.used to
  // support interrupt which have locking and unlocking functions
  auto converted = CF_EXPECT(
      acloud_impl::ConvertAcloudCreate(request, waiter_, cb_unlock, cb_lock));
  if (lock_released) {
    interrupt_lock.lock();
  }
  // currently, optout/optin feature only works in local instance
  // remote instance would continue to be done either through `python acloud` or
  // `cvdr` (if enabled).
  CF_EXPECT(!optout_);
  cvd::Response response;
  response.mutable_command_response();
  return response;
}

Result<cvd::Response> TryAcloudCommand::VerifyWithCvdRemote(
    const RequestWithStdio& request) {
  const uid_t uid = request.Credentials()->uid;
  auto filename = CF_EXPECT(GetDefaultConfigFile(uid));
  auto config = CF_EXPECT(LoadAcloudConfig(filename, uid));
  CF_EXPECT(config.use_cvdr == true);
  auto args = ParseInvocation(request.Message()).arguments;
  CF_EXPECT(acloud_impl::CompileFromAcloudToCvdr(args));
  cvd::Response response;
  response.mutable_command_response();
  return response;
}

std::unique_ptr<CvdServerHandler> NewTryAcloudCommand(
    std::atomic<bool>& optout) {
  return std::unique_ptr<CvdServerHandler>(new TryAcloudCommand(optout));
}

}  // namespace cuttlefish
