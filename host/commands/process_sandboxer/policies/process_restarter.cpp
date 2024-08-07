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

#include <sys/prctl.h>
#include <sys/socket.h>
#include <syscall.h>

#include <absl/strings/str_cat.h>
#include <absl/strings/str_replace.h>
#include <sandboxed_api/sandbox2/policybuilder.h>
#include <sandboxed_api/sandbox2/util/bpf_helper.h>

namespace cuttlefish::process_sandboxer {

sandbox2::PolicyBuilder ProcessRestarterPolicy(const HostInfo& host) {
  std::string sandboxer_proxy = host.HostToolExe("sandboxer_proxy");
  return BaselinePolicy(host, host.HostToolExe("process_restarter"))
      .AddDirectory(host.runtime_dir, /* is_ro= */ false)
      .AddFile(host.cuttlefish_config_path)
      .AddFileAt(sandboxer_proxy, host.HostToolExe("adb_connector"))
      .AddFileAt(sandboxer_proxy, host.HostToolExe("casimir"))
      .AddFileAt(sandboxer_proxy, host.HostToolExe("crosvm"))
      .AddFileAt(sandboxer_proxy, host.HostToolExe("root-canal"))
      .AddFileAt(sandboxer_proxy, host.HostToolExe("vhost_device_vsock"))
      .AddPolicyOnSyscall(__NR_prctl,
                          {ARG_32(0), JEQ32(PR_SET_PDEATHSIG, ALLOW)})
      .AllowFork()
      .AllowSafeFcntl()
      .AllowSyscall(SYS_execve)  // To enter sandboxer_proxy
      .AllowSyscall(SYS_waitid)
      // For sandboxer_proxy
      .AddPolicyOnSyscall(__NR_socket, {ARG_32(0), JEQ32(AF_UNIX, ALLOW)})
      .AllowExit()
      .AllowSyscall(SYS_connect)
      .AllowSyscall(SYS_recvmsg)
      .AllowSyscall(SYS_sendmsg);
}

}  // namespace cuttlefish::process_sandboxer
