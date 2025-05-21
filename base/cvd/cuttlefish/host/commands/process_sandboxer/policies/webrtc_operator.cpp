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
#include <netinet/ip_icmp.h>
#include <netinet/tcp.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/syscall.h>

#include <vector>

#include "sandboxed_api/sandbox2/allowlists/unrestricted_networking.h"
#include "sandboxed_api/sandbox2/policybuilder.h"
#include "sandboxed_api/sandbox2/util/bpf_helper.h"
#include "sandboxed_api/util/path.h"

namespace cuttlefish::process_sandboxer {

using sapi::file::JoinPath;

sandbox2::PolicyBuilder WebRtcOperatorPolicy(const HostInfo& host) {
  return BaselinePolicy(host, host.HostToolExe("webrtc_operator"))
      .AddDirectory(host.log_dir, /* is_ro= */ false)
      .AddDirectory(
          JoinPath(host.host_artifacts_path, "usr", "share", "webrtc"))
      .AddFile("/dev/urandom")  // For libwebsockets
      .AddFile(host.cuttlefish_config_path)
      .AllowEventFd()
      .AllowHandleSignals()
      .AddPolicyOnSyscall(
          __NR_madvise,
          {ARG_32(2), JEQ32(MADV_WIPEONFORK, ALLOW), JEQ32(0xffffffff, ALLOW)})
      .AddPolicyOnSyscall(__NR_prctl,
                          {ARG_32(0), JEQ32(PR_CAPBSET_READ, ALLOW)})
      .AddPolicyOnSyscall(__NR_socket, {ARG_32(0), JEQ32(AF_INET, ALLOW)})
      .AddPolicyOnSyscall(
          __NR_setsockopt,
          [](bpf_labels& labels) -> std::vector<sock_filter> {
            return {ARG_32(1),  // level
                    JEQ32(IPPROTO_ICMP,
                          JUMP(&labels, cf_webrtc_operator_setsockopt_icmp)),
                    JNE32(IPPROTO_TCP,
                          JUMP(&labels, cf_webrtc_operator_setsockopt_end)),
                    // IPPROTO_TCP
                    ARG_32(2),  // optname
                    JEQ32(TCP_NODELAY, ALLOW),
                    JUMP(&labels, cf_webrtc_operator_setsockopt_end),
                    // IPPROTO_ICMP
                    LABEL(&labels, cf_webrtc_operator_setsockopt_icmp),
                    ARG_32(2),  // optname
                    JEQ32(ICMP_REDIR_NETTOS, ALLOW),
                    LABEL(&labels, cf_webrtc_operator_setsockopt_end)};
          })
      .Allow(sandbox2::UnrestrictedNetworking())
      .AllowSafeFcntl()
      .AllowSyscall(__NR_accept)
      .AllowSyscall(__NR_bind)
      .AllowSyscall(__NR_getpeername)
      .AllowSyscall(__NR_getsockname)
      .AllowSyscall(__NR_listen)
      .AllowTCGETS();
}

}  // namespace cuttlefish::process_sandboxer
