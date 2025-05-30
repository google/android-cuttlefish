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

#include <stdlib.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <syscall.h>
#include <unistd.h>

#include <string>

#include "absl/log/check.h"
#include "sandboxed_api/sandbox2/policybuilder.h"
#include "sandboxed_api/sandbox2/util/bpf_helper.h"
#include "sandboxed_api/util/path.h"

namespace cuttlefish::process_sandboxer {

using sapi::file::JoinPath;

/*
 * This executable is built as a `python_binary_host`:
 * https://cs.android.com/android/platform/superproject/main/+/main:external/avb/Android.bp;l=136;drc=1bbcd661f0afe4ab56c7031f57d518a19015805e
 *
 * A `python_binary_host` executable is a python interpreter concatenated with a
 * zip file of the python code for this executable and the python standard
 * library.
 * https://cs.android.com/android/platform/superproject/main/+/main:build/soong/python/python.go;l=416;drc=4ce4f8893e5c5ee9b9b2669ceb36a01d85ea39f4
 *
 * Concatenation works because the interpreter is a ELF executable, identified
 * by an ELF prefix header, while zip files are identifier by a table added to
 * the file as a suffix.
 *
 * The python interpreter is an executable built out of the Android build system
 * with some custom code.
 * https://cs.android.com/android/platform/superproject/main/+/main:external/python/cpython3/android/launcher_main.cpp;drc=02afc01277f68e081dad208f2d660fc74d67be88
 */
sandbox2::PolicyBuilder AvbToolPolicy(const HostInfo& host) {
  /*
   * `launcher_main.cpp` relies on `android::base::GetExecutablePath()` which
   * tries to `readlink("/proc/self/exe")`. Sandbox2 doesn't mount a procfs at
   * /proc in the mount namespace, so we can do this mount ourselves.  However,
   * this specifically needs to appear inside the mount namespace as a symlink
   * so that `readlink` works correctly. Bind-mounting the file with `AddFileAt`
   * or even bind-mounting a symlink directly doesn't appear to work correctly
   * with `readlink`, so we have to bind-mount a parent directory into
   * /proc/self and create an `exe` symlink.
   *
   * https://cs.android.com/android/platform/superproject/main/+/main:system/libbase/file.cpp;l=491;drc=a4ac93b700ed623bdb333ccb2ac567b8a33081a7
   */
  std::string executable = host.HostToolExe("avbtool");

  char fake_proc_self[] = "/tmp/avbtool_XXXXXX";
  PCHECK(mkdtemp(fake_proc_self)) << "Failed to create fake /proc/self dir";
  PCHECK(symlink(executable.c_str(), JoinPath(fake_proc_self, "exe").c_str()) >=
         0)
      << "Failed to create 'exe' symlink for avbtool";
  return BaselinePolicy(host, executable)
      .AddDirectory(host.host_artifacts_path)
      .AddDirectory(host.guest_image_path)
      .AddDirectory(host.runtime_dir, /* is_ro= */ false)
      .AddDirectoryAt(fake_proc_self, "/proc/self")
      .AddFile("/dev/urandom")  // For Python
      .AddFileAt(host.HostToolExe("sandboxer_proxy"), "/usr/bin/openssl")
      // The executable `open`s itself to load the python files.
      .AddFile(executable)
      .AddLibrariesForBinary(host.HostToolExe("sandboxer_proxy"),
                             JoinPath(host.host_artifacts_path, "lib64"))
      .AddPolicyOnSyscall(__NR_ioctl, {ARG_32(1), JEQ32(TIOCGWINSZ, ALLOW)})
      .AddPolicyOnSyscall(__NR_socket, {ARG_32(0), JEQ32(AF_UNIX, ALLOW)})
      .AllowDup()
      .AllowEpoll()
      .AllowFork()
      .AllowHandleSignals()
      .AllowPipe()
      .AllowSafeFcntl()
      .AllowSyscall(__NR_connect)
      .AllowSyscall(__NR_mremap)
      .AllowSyscall(__NR_execve)
      .AllowSyscall(__NR_ftruncate)
      .AllowSyscall(__NR_recvmsg)
      .AllowSyscall(__NR_sendmsg)
      .AllowSyscall(__NR_sysinfo)
      .AllowTCGETS()
      .AllowWait();
}

}  // namespace cuttlefish::process_sandboxer
