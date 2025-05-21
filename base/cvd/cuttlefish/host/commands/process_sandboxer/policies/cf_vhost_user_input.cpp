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

#include "cuttlefish/host/commands/process_sandboxer/policies.h"

#include <sys/mman.h>
#include <syscall.h>

#include "sandboxed_api/sandbox2/policybuilder.h"
#include "sandboxed_api/sandbox2/util/bpf_helper.h"
#include "sandboxed_api/util/path.h"

namespace cuttlefish::process_sandboxer {

using sapi::file::JoinPath;

sandbox2::PolicyBuilder CfVhostUserInput(const HostInfo& host) {
  return BaselinePolicy(host, host.HostToolExe("cf_vhost_user_input"))
      .AddDirectory(host.runtime_dir, /* is_ro= */ false)
      .AddDirectory("/proc", /* is_ro= */ false)  // for inherited_fds
      .AddDirectory(
          JoinPath(host.host_artifacts_path, "etc", "default_input_devices"))
      .AddPolicyOnMmap([](bpf_labels& labels) -> std::vector<sock_filter> {
        return {
            ARG_32(2),  // prot
            JNE32(PROT_READ | PROT_WRITE,
                  JUMP(&labels, cf_vhost_user_input_mmap_end)),
            ARG_32(3),  // flags
            JEQ32(MAP_STACK | MAP_ANONYMOUS | MAP_PRIVATE, ALLOW),
            JEQ32(MAP_NORESERVE | MAP_SHARED, ALLOW),
            LABEL(&labels, cf_vhost_user_input_mmap_end),
        };
      })
      .AllowEpoll()
      .AllowEventFd()
      .AllowHandleSignals()
      .AllowReaddir()
      .AllowPrctlSetName()
      .AllowSyscall(__NR_accept4)
      .AllowSyscall(__NR_clone)
      .AllowSyscall(__NR_getrandom)
      .AllowSyscall(__NR_recvmsg)
      .AllowSyscall(__NR_sendmsg)
      .AllowSyscall(__NR_statx)
      .AllowSafeFcntl();
}

}  // namespace cuttlefish::process_sandboxer
