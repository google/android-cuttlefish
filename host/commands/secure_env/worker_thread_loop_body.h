//
// Copyright (C) 2023 The Android Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#pragma once

#include <functional>

#include "common/libs/fs/shared_fd.h"
#include "host/commands/secure_env/event_notifier.h"
#include "host/commands/secure_env/snapshot_running_flag.h"

namespace cuttlefish {
namespace secure_env_impl {

void WorkerInnerLoop(std::function<bool()> process_callback,
                     SnapshotRunningFlag& running, SharedFD read_fd,
                     SharedFD suspend_event_fd,
                     EventNotifier& suspended_notifier);

}  // namespace secure_env_impl
}  // namespace cuttlefish
