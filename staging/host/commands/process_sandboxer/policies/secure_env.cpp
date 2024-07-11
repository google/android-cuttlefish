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

#include <syscall.h>

#include "sandboxed_api/sandbox2/policybuilder.h"
#include "sandboxed_api/util/path.h"

using sapi::file::JoinPath;

namespace cuttlefish {
namespace process_sandboxer {

sandbox2::PolicyBuilder SecureEnvPolicy(const HostInfo& host) {
  auto exe = JoinPath(host.artifacts_path, "bin", "secure_env");
  return BaselinePolicy(host, exe)
      // ms-tpm-20-ref creates a NVChip file in the runtime directory
      .AddDirectory(host.runtime_dir, /* is_ro= */ false)
      .AddFile(host.cuttlefish_config_path)
      .AddFile(exe)  // to exec itself
      .AllowDup()
      .AllowFork()    // Something is using clone, not sure what
      .AllowGetIDs()  // For getuid
      .AllowSafeFcntl()
      .AllowSelect()
      .AllowSyscall(__NR_accept)
      .AllowSyscall(__NR_execve)  // to exec itself
      // Something is using arguments not allowed by AllowGetRandom()
      .AllowSyscall(__NR_getrandom)
      .AllowSyscall(__NR_madvise)
      // statx not covered by AllowStat()
      .AllowSyscall(__NR_statx)
      .AllowSyscall(__NR_socketpair)
      .AllowSyscall(__NR_tgkill)
      .AllowUnlink()  // keymint_secure_deletion_data
      .AllowTCGETS()
      .AllowTime();
}

}  // namespace process_sandboxer
}  // namespace cuttlefish
