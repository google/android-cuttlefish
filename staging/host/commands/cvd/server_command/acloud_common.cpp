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

#include "host/commands/cvd/server_command/acloud_common.h"

#include "host/commands/cvd/server_command/utils.h"

namespace cuttlefish {

bool IsSubOperationSupported(const RequestWithStdio& request) {
  auto invocation = ParseInvocation(request.Message());
  if (invocation.arguments.empty()) {
    return false;
  }
  return invocation.arguments[0] == "create";
}

}  // namespace cuttlefish
