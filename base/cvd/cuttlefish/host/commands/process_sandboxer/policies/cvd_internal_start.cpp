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

#include <linux/prctl.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/syscall.h>

#include <string>

#include "sandboxed_api/sandbox2/policybuilder.h"
#include "sandboxed_api/sandbox2/util/bpf_helper.h"

namespace cuttlefish::process_sandboxer {

sandbox2::PolicyBuilder CvdInternalStartPolicy(const HostInfo& host) {
  std::string sandboxer_proxy = host.HostToolExe("sandboxer_proxy");
  return BaselinePolicy(host, host.HostToolExe("cvd_internal_start"))
      .AddDirectory(host.assembly_dir)
      .AddDirectory(host.runtime_dir)
      .AddFile("/dev/null")
      .AddFileAt(sandboxer_proxy, host.HostToolExe("assemble_cvd"))
      .AddFileAt(sandboxer_proxy, host.HostToolExe("run_cvd"))
      .AddPolicyOnSyscall(__NR_madvise,
                          {ARG_32(2), JEQ32(MADV_DONTNEED, ALLOW)})
      .AddPolicyOnSyscall(__NR_prctl,
                          {ARG_32(0), JEQ32(PR_SET_PDEATHSIG, ALLOW)})
      .AllowDup()
      .AllowPipe()
      .AllowFork()
      .AllowSafeFcntl()
      .AllowSyscall(__NR_execve)
      .AllowSyscall(__NR_getcwd)
      .AllowSyscall(__NR_fchdir)
      .AllowWait()
      // sandboxer_proxy
      .AddPolicyOnSyscall(__NR_socket, {ARG_32(0), JEQ32(AF_UNIX, ALLOW)})
      .AllowSyscall(__NR_connect)
      .AllowSyscall(__NR_recvmsg)
      .AllowSyscall(__NR_sendmsg);
}

}  // namespace cuttlefish::process_sandboxer
