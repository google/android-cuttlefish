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

#include <cerrno>
#include <string>

#include "sandboxed_api/sandbox2/policybuilder.h"
#include "sandboxed_api/sandbox2/util/bpf_helper.h"
#include "sandboxed_api/util/path.h"

namespace cuttlefish::process_sandboxer {

using sapi::file::JoinPath;

sandbox2::PolicyBuilder AssembleCvdPolicy(const HostInfo& host) {
  std::string sandboxer_proxy = host.HostToolExe("sandboxer_proxy");
  return BaselinePolicy(host, host.HostToolExe("assemble_cvd"))
      .AddDirectory(host.assembly_dir, /* is_ro= */ false)
      // TODO(schuffelen): Don't resize vbmeta in-place
      .AddDirectory(host.guest_image_path, /* is_ro= */ false)
      .AddDirectory(
          JoinPath(host.host_artifacts_path, "etc", "bootloader_x86_64"))
      .AddDirectory(JoinPath(host.host_artifacts_path, "etc", "cvd_config"))
      // TODO(schuffelen): Copy these files before modifying them
      .AddDirectory(JoinPath(host.host_artifacts_path, "etc", "openwrt"),
                    /* is_ro= */ false)
      .AddDirectory(host.environments_dir, /* is_ro= */ false)
      .AddDirectory(host.EnvironmentsUdsDir(), /* is_ro= */ false)
      .AddDirectory(host.InstanceUdsDir(), /* is_ro= */ false)
      .AddDirectory("/tmp/cf_avd_1000", /* is_ro= */ false)
      .AddDirectory(host.runtime_dir, /* is_ro= */ false)
      .AddDirectory(host.tmp_dir, /* is_ro= */ false)
      .AddDirectory(host.VsockDeviceDir(), /* is_ro= */ false)
      // `webRTC` actually uses this file, but `assemble_cvd` first checks
      // whether it exists in order to decide whether to connect to it.
      .AddFile("/run/cuttlefish/operator")
      .AddFileAt(sandboxer_proxy, host.HostToolExe("avbtool"))
      .AddFileAt(sandboxer_proxy, host.HostToolExe("crosvm"))
      .AddFileAt(sandboxer_proxy, host.HostToolExe("mkenvimage_slim"))
      .AddFileAt(sandboxer_proxy, host.HostToolExe("newfs_msdos"))
      .AddFileAt(sandboxer_proxy, host.HostToolExe("simg2img"))
      .AddPolicyOnSyscall(__NR_madvise,
                          {ARG_32(2), JEQ32(MADV_DONTNEED, ALLOW)})
      .AddPolicyOnSyscall(__NR_prctl,
                          {ARG_32(0), JEQ32(PR_SET_PDEATHSIG, ALLOW)})
      /* sandboxer_proxy needs AF_UNIX. `assemble_cvd/network_flags.cpp` calls
       * `getifaddrs` which won't give any interesting output in the network
       * namespace anyway. */
      .AddPolicyOnSyscall(__NR_socket, {ARG_32(0), JEQ32(AF_UNIX, ALLOW),
                                        JEQ32(AF_INET, ERRNO(EACCES)),
                                        JEQ32(AF_NETLINK, ERRNO(EACCES))})
      .AllowDup()
      .AllowFork()
      .AllowGetIDs()
      .AllowLink()
      .AllowMkdir()
      .AllowPipe()
      .AllowReaddir()
      .AllowRename()
      .AllowSafeFcntl()
      .AllowSymlink()
      .AllowUnlink()
      .AllowSyscall(__NR_execve)
      .AllowSyscall(__NR_flock)
      .AllowSyscall(__NR_ftruncate)
      .AllowSyscall(__NR_fsync)
      .AllowSyscall(__NR_umask)
      .AllowTCGETS()
      .AllowWait()
      // For sandboxer_proxy
      .AllowExit()
      .AllowSyscall(SYS_connect)
      .AllowSyscall(SYS_recvmsg)
      .AllowSyscall(SYS_sendmsg);
}

}  // namespace cuttlefish::process_sandboxer
