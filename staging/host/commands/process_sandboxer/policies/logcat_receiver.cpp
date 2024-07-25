/*
 * Copyright (C) 2024 The Android Open Source Project
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

#include "host/commands/process_sandboxer/policies.h"

#include <sys/prctl.h>

#include "sandboxed_api/sandbox2/policybuilder.h"

namespace cuttlefish {
namespace process_sandboxer {

sandbox2::PolicyBuilder LogcatReceiverPolicy(const HostInfo& host) {
  return BaselinePolicy(host, host.HostToolExe("logcat_receiver"))
      .AddDirectory(host.log_dir, /* is_ro= */ false)
      .AddFile(host.cuttlefish_config_path)
      .AllowHandleSignals()
      .AllowOpen()
      .AllowRead()
      .AllowSafeFcntl()
      .AllowWrite();
}

}  // namespace process_sandboxer
}  // namespace cuttlefish
