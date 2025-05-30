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
#include <sys/socket.h>
#include <syscall.h>

#include <cerrno>
#include <vector>

#include "sandboxed_api/sandbox2/policybuilder.h"
#include "sandboxed_api/sandbox2/util/bpf_helper.h"

namespace cuttlefish::process_sandboxer {

sandbox2::PolicyBuilder ControlEnvProxyServerPolicy(const HostInfo& host) {
  return BaselinePolicy(host, host.HostToolExe("control_env_proxy_server"))
      .AddDirectory(host.InstanceUdsDir(), /* is_ro= */ false)
      .AddFile("/dev/urandom")  // For gRPC
      .AddPolicyOnSyscall(__NR_madvise,
                          {ARG_32(2), JEQ32(MADV_DONTNEED, ALLOW)})
      .AddPolicyOnSyscall(__NR_socket, {ARG_32(0), JEQ32(AF_UNIX, ALLOW),
                                        JEQ32(AF_INET, ERRNO(EACCES)),
                                        JEQ32(AF_INET6, ERRNO(EACCES))})
      .AddPolicyOnSyscalls(
          {__NR_getsockopt, __NR_setsockopt},
          [](bpf_labels& labels) -> std::vector<sock_filter> {
            return {
                ARG_32(1),  // level
                JNE32(SOL_SOCKET,
                      JUMP(&labels, cf_control_env_proxy_server_sockopt_end)),
                ARG_32(2),  // optname
                JEQ32(SO_REUSEPORT, ALLOW),
                LABEL(&labels, cf_control_env_proxy_server_sockopt_end),
            };
          })
      .AllowChmod()
      .AllowEventFd()
      .AllowReaddir()
      .AllowSafeFcntl()
      .AllowSleep()
      .AllowSyscall(__NR_accept)
      .AllowSyscall(__NR_bind)
      .AllowSyscall(__NR_clone)  // Multi-threading
      .AllowSyscall(__NR_connect)
      .AllowSyscall(__NR_getpeername)
      .AllowSyscall(__NR_getsockname)
      .AllowSyscall(__NR_listen)
      .AllowSyscall(__NR_recvmsg)
      .AllowSyscall(__NR_shutdown)
      .AllowSyscall(__NR_sendmsg)
      .AllowSyscall(__NR_sched_getparam)
      .AllowSyscall(__NR_sched_getscheduler)
      .AllowSyscall(__NR_sched_yield);
}

}  // namespace cuttlefish::process_sandboxer
