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

#include <linux/bpf_common.h>
#include <linux/filter.h>
#include <linux/prctl.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <syscall.h>
#include <unistd.h>

#include <cstdint>
#include <string>
#include <vector>

#include "absl/strings/str_cat.h"
#include "absl/strings/str_replace.h"
#include "sandboxed_api/sandbox2/policybuilder.h"
#include "sandboxed_api/sandbox2/util/bpf_helper.h"
#include "sandboxed_api/util/path.h"

namespace cuttlefish::process_sandboxer {

using sapi::file::JoinPath;

sandbox2::PolicyBuilder RunCvdPolicy(const HostInfo& host) {
  std::string sandboxer_proxy = host.HostToolExe("sandboxer_proxy");
  return BaselinePolicy(host, host.HostToolExe("run_cvd"))
      .AddDirectory(host.runtime_dir, /* is_ro= */ false)
      .AddDirectory(
          JoinPath(host.host_artifacts_path, "etc", "default_input_devices"))
      .AddFile(host.cuttlefish_config_path)
      .AddFile("/dev/null", /* is_ro= */ false)
      .AddFileAt(sandboxer_proxy, host.HostToolExe("adb_connector"))
      .AddFileAt(sandboxer_proxy, host.HostToolExe("casimir_control_server"))
      .AddFileAt(sandboxer_proxy, host.HostToolExe("cf_vhost_user_input"))
      .AddFileAt(sandboxer_proxy, host.HostToolExe("control_env_proxy_server"))
      .AddFileAt(sandboxer_proxy, host.HostToolExe("crosvm"))
      .AddFileAt(sandboxer_proxy, host.HostToolExe("echo_server"))
      .AddFileAt(sandboxer_proxy, host.HostToolExe("gnss_grpc_proxy"))
      .AddFileAt(sandboxer_proxy, host.HostToolExe("kernel_log_monitor"))
      .AddFileAt(sandboxer_proxy, host.HostToolExe("log_tee"))
      .AddFileAt(sandboxer_proxy, host.HostToolExe("logcat_receiver"))
      .AddFileAt(sandboxer_proxy, host.HostToolExe("metrics"))
      .AddFileAt(sandboxer_proxy, host.HostToolExe("modem_simulator"))
      .AddFileAt(sandboxer_proxy, host.HostToolExe("netsimd"))
      .AddFileAt(sandboxer_proxy, host.HostToolExe("openwrt_control_server"))
      .AddFileAt(sandboxer_proxy, host.HostToolExe("operator_proxy"))
      .AddFileAt(sandboxer_proxy, host.HostToolExe("process_restarter"))
      .AddFileAt(sandboxer_proxy, host.HostToolExe("screen_recording_server"))
      .AddFileAt(sandboxer_proxy, host.HostToolExe("secure_env"))
      .AddFileAt(sandboxer_proxy, host.HostToolExe("socket_vsock_proxy"))
      .AddFileAt(sandboxer_proxy, host.HostToolExe("tcp_connector"))
      .AddFileAt(sandboxer_proxy, host.HostToolExe("tombstone_receiver"))
      .AddFileAt(sandboxer_proxy, host.HostToolExe("webRTC"))
      .AddFileAt(sandboxer_proxy, host.HostToolExe("webrtc_operator"))
      .AddFileAt(sandboxer_proxy, host.HostToolExe("wmediumd"))
      .AddFileAt(sandboxer_proxy, host.HostToolExe("wmediumd_gen_config"))
      .AddDirectory(host.environments_dir)
      .AddDirectory(host.EnvironmentsUdsDir(), /* is_ro= */ false)
      .AddDirectory(host.InstanceUdsDir(), /* is_ro= */ false)
      .AddDirectory(host.VsockDeviceDir(), /* is_ro= */ false)
      // The UID inside the sandbox2 namespaces is always 1000.
      .AddDirectoryAt(host.EnvironmentsUdsDir(),
                      absl::StrReplaceAll(
                          host.EnvironmentsUdsDir(),
                          {{absl::StrCat("cf_env_", getuid()), "cf_env_1000"}}),
                      false)
      .AddDirectoryAt(host.InstanceUdsDir(),
                      absl::StrReplaceAll(
                          host.InstanceUdsDir(),
                          {{absl::StrCat("cf_avd_", getuid()), "cf_avd_1000"}}),
                      false)
      .AddPolicyOnSyscall(__NR_madvise,
                          {ARG_32(2), JEQ32(MADV_DONTNEED, ALLOW)})
      .AddPolicyOnSyscall(
          __NR_mknodat,
          [](bpf_labels& labels) -> std::vector<sock_filter> {
            return {
                ARG_32(2),
                // a <- a & S_IFMT // Mask to only the file type bits
                BPF_STMT(BPF_ALU + BPF_AND + BPF_K,
                         static_cast<uint32_t>(S_IFMT)),
                // Only allow `mkfifo`
                JNE32(S_IFIFO, JUMP(&labels, cf_mknodat_end)),
                ARG_32(3),
                JEQ32(0, ALLOW),
                LABEL(&labels, cf_mknodat_end),
            };
          })
      .AddPolicyOnSyscall(__NR_prctl,
                          {ARG_32(0), JEQ32(PR_SET_PDEATHSIG, ALLOW),
                           JEQ32(PR_SET_CHILD_SUBREAPER, ALLOW)})
      .AddPolicyOnSyscall(
          __NR_setsockopt,
          [](bpf_labels& labels) -> std::vector<sock_filter> {
            return {
                ARG_32(1),
                JNE32(SOL_SOCKET, JUMP(&labels, cf_setsockopt_end)),
                ARG_32(2),
                JEQ32(SO_REUSEADDR, ALLOW),
                JEQ32(SO_RCVTIMEO, ALLOW),
                LABEL(&labels, cf_setsockopt_end),
            };
          })
      .AddPolicyOnSyscall(__NR_socket, {ARG_32(0), JEQ32(AF_UNIX, ALLOW),
                                        JEQ32(AF_VSOCK, ALLOW)})
      .AllowChmod()
      .AllowDup()
      .AllowEventFd()
      .AllowFork()  // Multithreading, sandboxer_proxy, process monitor
      .AllowGetIDs()
      .AllowInotifyInit()
      .AllowMkdir()
      .AllowPipe()
      .AllowSafeFcntl()
      .AllowSelect()
      .AllowSyscall(__NR_accept)
      .AllowSyscall(__NR_bind)
      .AllowSyscall(__NR_connect)
      .AllowSyscall(__NR_execve)  // sandboxer_proxy
      .AllowSyscall(__NR_getsid)
      .AllowSyscall(__NR_inotify_add_watch)
      .AllowSyscall(__NR_inotify_rm_watch)
      .AllowSyscall(__NR_listen)
      .AllowSyscall(__NR_msgget)  // Metrics SysV RPC
      .AllowSyscall(__NR_msgsnd)  // Metrics SysV RPC
      .AllowSyscall(__NR_recvmsg)
      .AllowSyscall(__NR_sendmsg)
      .AllowSyscall(__NR_setpgid)
      .AllowSyscall(__NR_shutdown)
      .AllowSyscall(__NR_socketpair)
      .AllowSyscall(__NR_waitid)  // Not covered by `AllowWait()`
      .AllowTCGETS()
      .AllowUnlink()
      .AllowWait();
}

}  // namespace cuttlefish::process_sandboxer
