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
#include <linux/prctl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <syscall.h>

#include <vector>

#include "sandboxed_api/sandbox2/allowlists/unrestricted_networking.h"
#include "sandboxed_api/sandbox2/policybuilder.h"
#include "sandboxed_api/sandbox2/util/bpf_helper.h"
#include "sandboxed_api/util/path.h"

namespace cuttlefish::process_sandboxer {

using sapi::file::JoinPath;

sandbox2::PolicyBuilder NetsimdPolicy(const HostInfo& host) {
  return BaselinePolicy(host, host.HostToolExe("netsimd"))
      .AddDirectory(JoinPath(host.host_artifacts_path, "bin", "netsim-ui"))
      .AddDirectory(JoinPath(host.runtime_dir, "internal"), /* is_ro= */ false)
      .AddDirectory(host.tmp_dir, /* is_ro= */ false)
      .AddFile("/dev/urandom")  // For gRPC
      .AddPolicyOnSyscalls(
          {__NR_getsockopt, __NR_setsockopt},
          [](bpf_labels& labels) -> std::vector<sock_filter> {
            return {
                ARG_32(1),  // level
                JEQ32(IPPROTO_TCP, JUMP(&labels, cf_netsimd_getsockopt_tcp)),
                JEQ32(IPPROTO_IPV6, JUMP(&labels, cf_netsimd_getsockopt_ipv6)),
                JNE32(SOL_SOCKET, JUMP(&labels, cf_netsimd_getsockopt_end)),
                // SOL_SOCKET
                ARG_32(2),  // optname
                JEQ32(SO_REUSEADDR, ALLOW),
                JEQ32(SO_REUSEPORT, ALLOW),
                JUMP(&labels, cf_netsimd_getsockopt_end),
                // IPPROTO_TCP
                LABEL(&labels, cf_netsimd_getsockopt_tcp),
                ARG_32(2),  // optname
                JEQ32(TCP_NODELAY, ALLOW),
                JEQ32(TCP_USER_TIMEOUT, ALLOW),
                JUMP(&labels, cf_netsimd_getsockopt_end),
                // IPPROTO_IPV6
                LABEL(&labels, cf_netsimd_getsockopt_ipv6),
                ARG_32(2),  // optname
                JEQ32(IPV6_V6ONLY, ALLOW),
                LABEL(&labels, cf_netsimd_getsockopt_end),
            };
          })
      .AddPolicyOnSyscall(__NR_madvise,
                          {ARG_32(2), JEQ32(MADV_DONTNEED, ALLOW)})
      .AddPolicyOnSyscall(__NR_prctl,
                          {ARG_32(0), JEQ32(PR_CAPBSET_READ, ALLOW)})
      .AddPolicyOnSyscall(__NR_socket, {ARG_32(0), JEQ32(AF_INET, ALLOW),
                                        JEQ32(AF_INET6, ALLOW)})
      .AddTmpfs("/tmp", 1 << 20)
      .Allow(sandbox2::UnrestrictedNetworking())
      .AllowDup()
      .AllowEpoll()
      .AllowEpollWait()
      .AllowEventFd()
      .AllowHandleSignals()
      .AllowMkdir()
      .AllowPipe()
      .AllowPrctlSetName()
      .AllowReaddir()
      .AllowSafeFcntl()
      .AllowSelect()
      .AllowSleep()
      .AllowSyscall(__NR_accept4)
      .AllowSyscall(__NR_bind)
      .AllowSyscall(__NR_clone)
      .AllowSyscall(__NR_getcwd)
      .AllowSyscall(__NR_getrandom)
      .AllowSyscall(__NR_getsockname)
      .AllowSyscall(__NR_listen)
      .AllowSyscall(__NR_sched_getparam)
      .AllowSyscall(__NR_sched_getscheduler)
      .AllowSyscall(__NR_sched_yield)
      .AllowSyscall(__NR_statx);  // Not covered by AllowStat
}

}  // namespace cuttlefish::process_sandboxer
