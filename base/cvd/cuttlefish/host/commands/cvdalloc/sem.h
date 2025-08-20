/*
 * Copyright (C) 2025 The Android Open Source Project
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

#include <chrono>

#include "cuttlefish/common/libs/fs/shared_fd.h"
#include "cuttlefish/common/libs/utils/result.h"

namespace cuttlefish {

constexpr std::chrono::seconds kSemNoTimeout = std::chrono::seconds(0);

/*
 * Write into the socket.
 *
 * Any other processes Wait'ing to read on the socket will be unblocked.
 */
Result<void> Post(const SharedFD socket);

/*
 * Wait on the socket.
 *
 * The process wil block until the socket can be read from, the socket is
 * shut down, or the timeout is reached. The Result is ok if the socket
 * can be successfully read from.
 */
Result<void> Wait(const SharedFD socket, std::chrono::seconds timeout);

}  // namespace
