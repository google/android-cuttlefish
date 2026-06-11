/*
 * Copyright (C) 2026 The Android Open Source Project
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

#include "cuttlefish/common/libs/fs/shared_fd.h"
#include "cuttlefish/host/commands/cvd/instances/local_instance.h"
#include "cuttlefish/result/result.h"

namespace cuttlefish {

void ClearLastNLines(int n);

Result<void> MonitorLogs(const LocalInstance& instance);

// The monitor will stop and return if `stop_eventfd` becomes readable (receives
// an event).
Result<void> MonitorLogs(const LocalInstance& instance, SharedFD stop_eventfd);

}  // namespace cuttlefish
