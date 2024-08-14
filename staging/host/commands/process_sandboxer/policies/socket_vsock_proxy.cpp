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

#include <sys/socket.h>
#include <syscall.h>

#include <sandboxed_api/sandbox2/allow_unrestricted_networking.h>
#include <sandboxed_api/sandbox2/policybuilder.h>
#include <sandboxed_api/sandbox2/util/bpf_helper.h>

namespace cuttlefish::process_sandboxer {

sandbox2::PolicyBuilder SocketVsockProxyPolicy(const HostInfo& host) {
  return BaselinePolicy(host, host.HostToolExe("socket_vsock_proxy"))
      .AddDirectory(host.log_dir, /* is_ro= */ false)
      .AddFile(host.cuttlefish_config_path)
      .AddPolicyOnSyscall(__NR_socket,
                          {ARG_32(0), JEQ32(AF_INET, ALLOW),
                           JEQ32(AF_INET6, ALLOW), JEQ32(AF_VSOCK, ALLOW)})
      .Allow(sandbox2::UnrestrictedNetworking())
      .AllowEventFd()
      .AllowFork()  // `clone` for multithreading
      .AllowHandleSignals()
      .AllowSafeFcntl()
      .AllowSyscall(__NR_bind)
      .AllowSyscall(__NR_connect)
      .AllowSyscall(__NR_listen)
      .AllowSyscall(__NR_setsockopt)
      .AllowSyscall(__NR_shutdown)
      .AllowSyscalls({__NR_accept, __NR_accept4})
      .AllowTCGETS();
}

}  // namespace cuttlefish::process_sandboxer
