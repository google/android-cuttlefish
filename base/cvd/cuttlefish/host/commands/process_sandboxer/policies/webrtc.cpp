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
#include <linux/sockios.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/ioctl.h>
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

sandbox2::PolicyBuilder WebRtcPolicy(const HostInfo& host) {
  return BaselinePolicy(host, host.HostToolExe("webRTC"))
      .AddDirectory(host.log_dir, /* is_ro= */ false)
      .AddDirectory(
          JoinPath(host.host_artifacts_path, "/usr/share/webrtc/assets"))
      .AddDirectory(host.InstanceUdsDir(), /* is_ro= */ false)
      .AddDirectory(host.VsockDeviceDir(), /* is_ro= */ false)
      .AddDirectory(JoinPath(host.runtime_dir, "recording"), /* is_ro= */ false)
      .AddFile(host.cuttlefish_config_path)
      .AddFile("/dev/urandom")
      .AddFile("/run/cuttlefish/operator")
      // Shared memory with crosvm for audio
      .AddPolicyOnMmap([](bpf_labels& labels) -> std::vector<sock_filter> {
        return {
            ARG_32(2),  // prot
            JNE32(PROT_READ | PROT_WRITE, JUMP(&labels, cf_webrtc_mmap_end)),
            ARG_32(3),  // flags
            JEQ32(MAP_SHARED, ALLOW),
            LABEL(&labels, cf_webrtc_mmap_end),
        };
      })
      .AddPolicyOnSyscall(
          __NR_getsockopt,
          [](bpf_labels& labels) -> std::vector<sock_filter> {
            return {
                ARG_32(1),  // level
                JNE32(SOL_SOCKET, JUMP(&labels, cf_webrtc_getsockopt_end)),
                ARG_32(2),  // optname
                JEQ32(SO_ERROR, ALLOW),
                JEQ32(SO_PEERCRED, ALLOW),
                LABEL(&labels, cf_webrtc_getsockopt_end),
            };
          })
      .AddPolicyOnSyscall(__NR_ioctl, {ARG_32(1), JEQ32(SIOCGSTAMP, ALLOW),
                                       JEQ32(FIONREAD, ALLOW)})
      .AddPolicyOnSyscall(__NR_madvise,
                          {ARG_32(2), JEQ32(MADV_WIPEONFORK, ALLOW),
                           JEQ32(MADV_DONTNEED, ALLOW),
                           // TODO: schuffelen - find out what this is
                           JEQ32(0xffffffff, ALLOW)})
      .AddPolicyOnSyscall(__NR_prctl,
                          {ARG_32(0), JEQ32(PR_CAPBSET_READ, ALLOW)})
      .AddPolicyOnSyscall(
          __NR_setsockopt,
          [](bpf_labels& labels) -> std::vector<sock_filter> {
            return {
                ARG_32(1),  // level
                JEQ32(SOL_IP, JUMP(&labels, cf_webrtc_setsockopt_ip)),
                JEQ32(SOL_IPV6, JUMP(&labels, cf_webrtc_setsockopt_ipv6)),
                JEQ32(SOL_SOCKET, JUMP(&labels, cf_webrtc_setsockopt_sol)),
                JNE32(IPPROTO_TCP, JUMP(&labels, cf_webrtc_setsockopt_end)),
                // IPPROTO_TCP
                ARG_32(2),  // optname
                JEQ32(TCP_NODELAY, ALLOW),
                JUMP(&labels, cf_webrtc_setsockopt_end),
                // SOL_IP
                LABEL(&labels, cf_webrtc_setsockopt_ip),
                ARG_32(2),  // optname
                JEQ32(IP_RECVERR, ALLOW),
                JEQ32(IP_TOS, ALLOW),
                JEQ32(IP_RETOPTS, ALLOW),
                JEQ32(IP_PKTINFO, ALLOW),
                JUMP(&labels, cf_webrtc_setsockopt_end),
                // SOL_IPV6
                LABEL(&labels, cf_webrtc_setsockopt_ipv6),
                ARG_32(2),  // optname
                JEQ32(IPV6_TCLASS, ALLOW),
                JUMP(&labels, cf_webrtc_setsockopt_end),
                // SOL_SOCKET
                LABEL(&labels, cf_webrtc_setsockopt_sol),
                ARG_32(2),  // optname
                JEQ32(SO_REUSEADDR, ALLOW),
                JEQ32(SO_SNDBUF, ALLOW),
                JEQ32(SO_RCVBUF, ALLOW),
                LABEL(&labels, cf_webrtc_setsockopt_end),
            };
          })
      .AddPolicyOnSyscall(
          __NR_socket, {ARG_32(0), JEQ32(AF_INET, ALLOW), JEQ32(AF_UNIX, ALLOW),
                        JEQ32(AF_INET6, ALLOW),
                        // webrtc/rtc_base/ifaddrs_android.cc
                        JEQ32(AF_NETLINK, ALLOW), JEQ32(AF_VSOCK, ALLOW)})
      .Allow(sandbox2::UnrestrictedNetworking())
      .AllowEpoll()
      .AllowEpollWait()
      .AllowEventFd()
      .AllowGetIDs()
      .AllowHandleSignals()
      .AllowPipe()
      .AllowPrctlSetName()
      .AllowSafeFcntl()
      .AllowSelect()
      .AllowSleep()
      .AllowSyscall(__NR_accept)
      .AllowSyscall(__NR_accept4)
      .AllowSyscall(__NR_bind)
      .AllowSyscall(__NR_clone)  // Multithreading
      .AllowSyscall(__NR_connect)
      .AllowSyscall(__NR_ftruncate)
      .AllowSyscall(__NR_getpeername)
      .AllowSyscall(__NR_getsockname)
      .AllowSyscall(__NR_listen)
      .AllowSyscall(__NR_memfd_create)
      .AllowSyscall(__NR_recvfrom)
      .AllowSyscall(__NR_recvmsg)
      .AllowSyscall(__NR_sched_get_priority_max)
      .AllowSyscall(__NR_sched_get_priority_min)
      .AllowSyscall(__NR_sched_getparam)
      .AllowSyscall(__NR_sched_getscheduler)
      .AllowSyscall(__NR_sched_setscheduler)
      .AllowSyscall(__NR_sched_yield)
      .AllowSyscall(__NR_sendmsg)
      .AllowSyscall(__NR_sendmmsg)
      .AllowSyscall(__NR_sendto)
      .AllowSyscall(__NR_shutdown)
      .AllowSyscall(__NR_socketpair)
      .AllowTCGETS();
}

}  // namespace cuttlefish::process_sandboxer
