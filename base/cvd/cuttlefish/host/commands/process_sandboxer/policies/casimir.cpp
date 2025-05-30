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
#include <netinet/ip_icmp.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/syscall.h>

#include <vector>

#include "sandboxed_api/sandbox2/policybuilder.h"
#include "sandboxed_api/sandbox2/util/bpf_helper.h"

namespace cuttlefish::process_sandboxer {

sandbox2::PolicyBuilder CasimirPolicy(const HostInfo& host) {
  return BaselinePolicy(host, host.HostToolExe("casimir"))
      // `librustutils::inherited_fd` scans `/proc/self/fd` for open FDs.
      // Mounting a subset of `/proc/` is invalid.
      .AddDirectory("/proc", /* is_ro = */ false)
      .AddDirectory(host.EnvironmentsUdsDir(), /* is_ro= */ false)
      .AddPolicyOnMmap([](bpf_labels& labels) -> std::vector<sock_filter> {
        return {
            ARG_32(2),  // prot
            JNE32(PROT_READ | PROT_WRITE, JUMP(&labels, cf_casimir_mmap_end)),
            ARG_32(3),  // flags
            JEQ32(MAP_PRIVATE | MAP_ANONYMOUS | MAP_STACK, ALLOW),
            LABEL(&labels, cf_casimir_mmap_end),
        };
      })
      .AddPolicyOnSyscall(
          __NR_setsockopt,
          [](bpf_labels& labels) -> std::vector<sock_filter> {
            return {
                ARG_32(1),  // level
                JNE32(IPPROTO_ICMP, JUMP(&labels, cf_casimir_setsockopt_end)),
                // IPPROTO_ICMP
                ARG_32(2),  // optname
                JEQ32(ICMP_REDIR_NETTOS, ALLOW),
                LABEL(&labels, cf_casimir_setsockopt_end)};
          })
      .AddPolicyOnSyscall(__NR_ioctl, {ARG_32(1), JEQ32(FIONBIO, ALLOW)})
      .AddPolicyOnSyscall(__NR_socket, {ARG_32(0), JEQ32(AF_UNIX, ALLOW)})
      .AllowEpoll()
      .AllowEpollWait()
      .AllowEventFd()
      .AllowHandleSignals()
      .AllowPrctlSetName()
      .AllowReaddir()
      .AllowSafeFcntl()
      .AllowSyscall(__NR_accept4)
      .AllowSyscall(__NR_bind)
      .AllowSyscall(__NR_clone)
      .AllowSyscall(__NR_listen)
      // Uses GRND_INSECURE which is not covered by AllowGetRandom()
      .AllowSyscall(__NR_getrandom)
      .AllowSyscall(__NR_recvfrom)
      .AllowSyscall(__NR_sendto)
      .AllowSyscall(__NR_shutdown)
      .AllowSyscall(__NR_statx);  // Not covered by AllowStat
}

}  // namespace cuttlefish::process_sandboxer
