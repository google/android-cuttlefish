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
#include <sandboxed_api/sandbox2/trace_all_syscalls.h>
#include <sandboxed_api/sandbox2/util/bpf_helper.h>

namespace cuttlefish::process_sandboxer {

sandbox2::PolicyBuilder AdbConnectorPolicy(const HostInfo& host) {
  return BaselinePolicy(host, host.HostToolExe("adb_connector"))
      .AddDirectory(host.log_dir, /* is_ro= */ false)
      .AddFile(host.cuttlefish_config_path)
      .Allow(sandbox2::UnrestrictedNetworking())  // Used to message adb server
      .AddPolicyOnSyscall(__NR_socket, {ARG_32(0), JEQ32(AF_INET, ALLOW),
                                        JEQ32(AF_UNIX, ALLOW)})
      .AllowSafeFcntl()
      .AllowSleep()
      .AllowSyscall(__NR_clone)  // Multithreading
      .AllowSyscall(__NR_connect)
      .AllowSyscall(__NR_recvmsg)
      .AllowSyscall(__NR_sendmsg)
      .AllowSyscall(__NR_sendto)
      .AllowTCGETS();
}

}  // namespace cuttlefish::process_sandboxer
