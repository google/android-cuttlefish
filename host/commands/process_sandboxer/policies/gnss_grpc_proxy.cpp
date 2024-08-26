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

#include <errno.h>
#include <sys/syscall.h>

#include <sandboxed_api/sandbox2/policybuilder.h>
#include <sandboxed_api/sandbox2/util/bpf_helper.h>

namespace cuttlefish::process_sandboxer {

sandbox2::PolicyBuilder GnssGrpcProxyPolicy(const HostInfo& host) {
  return BaselinePolicy(host, host.HostToolExe("gnss_grpc_proxy"))
      .AddDirectory(host.instance_uds_dir, /* is_ro= */ false)
      .AddDirectory(host.log_dir, /* is_ro= */ false)
      .AddFile("/dev/urandom")  // For gRPC
      .AddFile(host.cuttlefish_config_path)
      .AddPolicyOnSyscall(__NR_socket, {ARG_32(0), JEQ32(AF_UNIX, ALLOW)})
      .AllowSyscall(__NR_socket)
      .AllowEventFd()
      .AllowSafeFcntl()
      .AllowSleep()
      .AllowSyscall(__NR_bind)
      .AllowSyscall(__NR_clone)  // multithreading
      .AllowSyscall(__NR_getpeername)
      .AllowSyscall(__NR_getsockname)
      .AllowSyscall(__NR_getsockopt)  // TODO: restrict
      .AllowSyscall(__NR_listen)
      .AllowSyscall(__NR_madvise)  // TODO: b/318591948 - restrict or remove
      .AllowSyscall(__NR_recvmsg)
      .AllowSyscall(__NR_sched_getparam)
      .AllowSyscall(__NR_sched_getscheduler)
      .AllowSyscall(__NR_sched_yield)
      .AllowSyscall(__NR_shutdown)
      .AllowSyscall(__NR_sendmsg)
      .AllowSyscall(__NR_setsockopt)  // TODO: restrict
      .AllowSyscalls({__NR_accept, __NR_accept4})
      .AllowTCGETS();
}

}  // namespace cuttlefish::process_sandboxer
