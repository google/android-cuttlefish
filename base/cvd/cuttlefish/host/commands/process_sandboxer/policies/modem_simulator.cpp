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
#include <sys/socket.h>
#include <syscall.h>

#include <vector>

#include "sandboxed_api/sandbox2/policybuilder.h"
#include "sandboxed_api/sandbox2/util/bpf_helper.h"
#include "sandboxed_api/util/path.h"

namespace cuttlefish::process_sandboxer {

using sapi::file::JoinPath;

sandbox2::PolicyBuilder ModemSimulatorPolicy(const HostInfo& host) {
  return BaselinePolicy(host, host.HostToolExe("modem_simulator"))
      .AddDirectory(JoinPath(host.host_artifacts_path, "/etc/modem_simulator"))
      .AddDirectory(host.log_dir, /* is_ro= */ false)
      .AddDirectory(host.runtime_dir, /* is_ro= */ false)  // modem_nvram.json
      .AddFile(host.cuttlefish_config_path)
      .AddPolicyOnSyscall(
          __NR_setsockopt,
          [](bpf_labels& labels) -> std::vector<sock_filter> {
            return {
                ARG_32(1),
                JNE32(SOL_SOCKET, JUMP(&labels, cf_setsockopt_end)),
                ARG_32(2),
                JEQ32(SO_REUSEADDR, ALLOW),
                LABEL(&labels, cf_setsockopt_end),
            };
          })
      .AddPolicyOnSyscall(__NR_socket, {ARG_32(0), JEQ32(AF_UNIX, ALLOW)})
      .AllowHandleSignals()
      .AllowPipe()
      .AllowSafeFcntl()
      .AllowSelect()
      .AllowSyscall(__NR_accept)
      .AllowSyscall(__NR_bind)
      .AllowSyscall(__NR_clone)  // multithreading
      .AllowSyscall(__NR_listen)
      .AllowTCGETS();
}

}  // namespace cuttlefish::process_sandboxer
