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

#pragma once

#include <mutex>
#include <string>
#include <vector>

#include "host/commands/cvd/server_client.h"
#include "host/commands/cvd/server_command/subprocess_waiter.h"

namespace cuttlefish {

struct ConvertedAcloudCreateCommand {
  std::vector<RequestWithStdio> prep_requests;
  RequestWithStdio start_request;
  std::string fetch_command_str;
  std::string fetch_cvd_args_file;
  bool verbose;
  bool interrupt_lock_released;
};

namespace acloud_impl {

/*
 * Converts the acloud create commands.
 *
 * Given that the lock is already acquired, it may start a subprocess
 * using waiter. If it runs multiple subprocesses in turn using the same
 * waiter, it acquire the lock before Start() and release the lock before
 * Wait(). The interrupt_lock_released in the return value says whether
 * the lock is released or not.
 * The input parameters waiter, callback_unlock and callback_lock
 * provide locking system to support interrupt.
 *
 */
Result<ConvertedAcloudCreateCommand> ConvertAcloudCreate(
    const RequestWithStdio& request, SubprocessWaiter& waiter,
    std::function<Result<void>(void)> callback_unlock,
    std::function<Result<void>(void)> callback_lock);

}  // namespace acloud_impl
}  // namespace cuttlefish
