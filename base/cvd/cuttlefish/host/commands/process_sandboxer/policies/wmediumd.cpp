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
#include "sandboxed_api/util/path.h"

namespace cuttlefish::process_sandboxer {

using sapi::file::JoinPath;

sandbox2::PolicyBuilder WmediumdPolicy(const HostInfo& host) {
  return BaselinePolicy(host, host.HostToolExe("wmediumd"))
      .AddDirectory(host.EnvironmentsUdsDir(), /* is_ro= */ false)
      .AddDirectory(host.InstanceUdsDir(), /* is_ro= */ false)
      .AddDirectory(host.log_dir, /* is_ro= */ false)
      .AddFile("/dev/urandom")  // For gRPC
      .AddFile(JoinPath(host.environments_dir, "env-1", "wmediumd.cfg"),
               /* is_ro= */ false)
      .AddFile(host.cuttlefish_config_path)
      // Shared memory with crosvm for wifi
      .AddPolicyOnMmap([](bpf_labels& labels) -> std::vector<sock_filter> {
        return {
            ARG_32(2),  // prot
            JNE32(PROT_READ | PROT_WRITE, JUMP(&labels, cf_wmediumd_mmap_end)),
            ARG_32(3),  // flags
            JEQ32(MAP_SHARED, ALLOW),
            LABEL(&labels, cf_wmediumd_mmap_end),
        };
      })
      .AddPolicyOnSyscalls(
          {__NR_getsockopt, __NR_setsockopt},
          [](bpf_labels& labels) -> std::vector<sock_filter> {
            return {
                ARG_32(1),  // level
                JNE32(SOL_SOCKET, JUMP(&labels, cf_wmediumd_getsockopt_end)),
                ARG_32(2),  // optname
                JEQ32(SO_REUSEPORT, ALLOW),
                LABEL(&labels, cf_wmediumd_getsockopt_end),
            };
          })
      .AddPolicyOnSyscall(__NR_madvise,
                          {ARG_32(2), JEQ32(MADV_DONTNEED, ALLOW)})
      // Unclear what's creating the INET and INET6 sockets
      .AddPolicyOnSyscall(__NR_socket, {ARG_32(0), JEQ32(AF_UNIX, ALLOW),
                                        JEQ32(AF_INET, ERRNO(EACCES)),
                                        JEQ32(AF_INET6, ERRNO(EACCES))})
      .AllowEventFd()
      .AllowHandleSignals()
      .AllowSafeFcntl()
      .AllowSelect()
      .AllowSleep()
      .AllowSyscall(__NR_accept)
      .AllowSyscall(__NR_bind)
      .AllowSyscall(__NR_clone)  // Multithreading
      .AllowSyscall(__NR_getpeername)
      .AllowSyscall(__NR_getsockname)
      .AllowSyscall(__NR_listen)
      .AllowSyscall(__NR_msgget)
      .AllowSyscall(__NR_msgsnd)
      .AllowSyscall(__NR_msgrcv)
      .AllowSyscall(__NR_recvmsg)
      .AllowSyscall(__NR_sched_getparam)
      .AllowSyscall(__NR_sched_getscheduler)
      .AllowSyscall(__NR_sched_yield)
      .AllowSyscall(__NR_sendmsg)
      .AllowSyscall(__NR_shutdown)
      .AllowSyscall(__NR_timerfd_create)
      .AllowSyscall(__NR_timerfd_settime);
}

}  // namespace cuttlefish::process_sandboxer
