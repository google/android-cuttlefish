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

#include "cuttlefish/host/commands/process_sandboxer/policies.h"

#include <linux/filter.h>
#include <sys/mman.h>

#include <string_view>
#include <vector>

#include "sandboxed_api/sandbox2/policybuilder.h"
#include "sandboxed_api/sandbox2/util/bpf_helper.h"
#include "sandboxed_api/util/path.h"

namespace cuttlefish::process_sandboxer {

using sapi::file::JoinPath;

sandbox2::PolicyBuilder BaselinePolicy(const HostInfo& host,
                                       std::string_view exe) {
  return sandbox2::PolicyBuilder()
      .AddLibrariesForBinary(exe, JoinPath(host.host_artifacts_path, "lib64"))
      // For dynamic linking and memory allocation
      .AllowDynamicStartup()
      .AllowExit()
      .AllowGetPIDs()
      .AllowGetRandom()
      // Observed by `strace` on `socket_vsock_proxy` with x86_64 AOSP `glibc`.
      .AddPolicyOnMmap([](bpf_labels& labels) -> std::vector<sock_filter> {
        return {
            ARG_32(2),  // prot
            JEQ32(PROT_NONE, JUMP(&labels, cf_mmap_prot_none)),
            JEQ32(PROT_READ, JUMP(&labels, cf_mmap_prot_read)),
            JEQ32(PROT_READ | PROT_EXEC, JUMP(&labels, cf_mmap_prot_read_exec)),
            JNE32(PROT_READ | PROT_WRITE, JUMP(&labels, cf_mmap_prot_end)),
            // PROT_READ | PROT_WRITE
            ARG_32(3),  // flags
            JEQ32(MAP_PRIVATE | MAP_FIXED | MAP_ANONYMOUS, ALLOW),
            JUMP(&labels, cf_mmap_prot_end),
            // PROT_READ | PROT_EXEC
            LABEL(&labels, cf_mmap_prot_read_exec),
            ARG_32(3),  // flags
            JEQ32(MAP_PRIVATE | MAP_DENYWRITE, ALLOW),
            JEQ32(MAP_PRIVATE | MAP_FIXED | MAP_DENYWRITE, ALLOW),
            JUMP(&labels, cf_mmap_prot_end),
            // PROT_READ
            LABEL(&labels, cf_mmap_prot_read),
            ARG_32(3),  // flags
            JEQ32(MAP_PRIVATE | MAP_ANONYMOUS, ALLOW),
            JEQ32(MAP_PRIVATE | MAP_DENYWRITE, ALLOW),
            JEQ32(MAP_PRIVATE | MAP_FIXED | MAP_DENYWRITE, ALLOW),
            JEQ32(MAP_PRIVATE, ALLOW),
            JUMP(&labels, cf_mmap_prot_end),
            // PROT_NONE
            LABEL(&labels, cf_mmap_prot_none),
            ARG_32(3),  // flags
            JEQ32(MAP_PRIVATE | MAP_ANONYMOUS | MAP_STACK, ALLOW),
            JEQ32(MAP_PRIVATE | MAP_ANONYMOUS, ALLOW),

            LABEL(&labels, cf_mmap_prot_end),
        };
      })
      .AllowReadlink()
      .AllowRestartableSequences(sandbox2::PolicyBuilder::kAllowSlowFences)
      .AllowWrite();
}

}  // namespace cuttlefish::process_sandboxer
