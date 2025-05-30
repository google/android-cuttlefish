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
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <syscall.h>

#include <vector>

#include "sandboxed_api/sandbox2/policybuilder.h"
#include "sandboxed_api/sandbox2/util/bpf_helper.h"

namespace cuttlefish::process_sandboxer {

sandbox2::PolicyBuilder VhostDeviceVsockPolicy(const HostInfo& host) {
  return BaselinePolicy(host, host.HostToolExe("vhost_device_vsock"))
      .AddDirectory(host.VsockDeviceDir(), /* is_ro= */ false)
      .AddPolicyOnMmap([](bpf_labels& labels) -> std::vector<sock_filter> {
        return {
            ARG_32(2),  // prot
            JNE32(PROT_READ | PROT_WRITE, JUMP(&labels, cf_webrtc_mmap_end)),
            ARG_32(3),  // flags
            JEQ32(MAP_STACK | MAP_PRIVATE | MAP_ANONYMOUS, ALLOW),
            JEQ32(MAP_NORESERVE | MAP_SHARED, ALLOW),
            LABEL(&labels, cf_webrtc_mmap_end),
        };
      })
      .AddPolicyOnSyscall(__NR_ioctl, {ARG_32(1), JEQ32(FIONBIO, ALLOW)})
      .AddPolicyOnSyscall(__NR_socket, {ARG_32(0), JEQ32(AF_UNIX, ALLOW)})
      .AllowDup()
      .AllowEpoll()
      .AllowEpollWait()
      .AllowEventFd()
      .AllowHandleSignals()
      .AllowPrctlSetName()
      .AllowSafeFcntl()
      .AllowSyscall(__NR_accept4)
      .AllowSyscall(__NR_bind)
      .AllowSyscall(__NR_clone)
      .AllowSyscall(__NR_connect)
      .AllowSyscall(__NR_getrandom)  // AllowGetRandom won't take GRND_INSECURE
      .AllowSyscall(__NR_listen)
      .AllowSyscall(__NR_recvfrom)
      .AllowSyscall(__NR_recvmsg)
      .AllowSyscall(__NR_sendmsg)
      .AllowUnlink();
}

}  // namespace cuttlefish::process_sandboxer
