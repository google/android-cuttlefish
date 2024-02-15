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
#include "sandboxed_api/sandbox2/util/bpf_helper.h"
#include "sandboxed_api/util/path.h"

using sapi::file::JoinPath;

namespace cuttlefish {

sandbox2::PolicyBuilder LogcatReceiverPolicy(const HostInfo& host) {
  auto exe = JoinPath(host.artifacts_path, "bin", "logcat_receiver");
  auto lib64 = JoinPath(host.artifacts_path, "lib64");
  return sandbox2::PolicyBuilder()
      .AddDirectory(lib64)
      .AddDirectory(host.log_dir, /* is_ro= */ false)
      .AddFile(host.cuttlefish_config_path)
      .AddLibrariesForBinary(exe, lib64)
      // For dynamic linking
      .AddPolicyOnSyscall(__NR_prctl,
                          {ARG_32(0), JEQ32(PR_CAPBSET_READ, ALLOW)})
      .AllowDynamicStartup()
      .AllowExit()
      .AllowGetPIDs()
      .AllowGetRandom()
      .AllowHandleSignals()
      .AllowMmap()
      .AllowOpen()
      .AllowRead()
      .AllowReadlink()
      .AllowRestartableSequences(sandbox2::PolicyBuilder::kAllowSlowFences)
      .AllowSafeFcntl()
      .AllowSyscall(__NR_tgkill)
      .AllowWrite();
}

}  // namespace cuttlefish
