/*
 * Copyright (C) 2026 The Android Open Source Project
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

#include <limits.h>
#include <sys/param.h>
#include <sys/sysctl.h>
#include <sys/types.h>
#include <unistd.h>

#include <iterator>
#include <string>
#include <unordered_map>
#include <vector>

#include "absl/strings/str_split.h"
#include "absl/strings/string_view.h"
#include "android-base/file.h"

#include "cuttlefish/common/libs/utils/files.h"
#include "cuttlefish/process/proc_file_utils.h"
#include "cuttlefish/process/proc_file_utils_sysctl.h"
#include "cuttlefish/result/result.h"

namespace cuttlefish {
namespace {

Result<struct kinfo_proc> KinfoProc(const pid_t pid) {
  // OpenBSD's KERN_PROC takes the element size and count as the last two names.
  int mib[] = {
      CTL_KERN, KERN_PROC, KERN_PROC_PID, pid, sizeof(struct kinfo_proc), 1};
  std::vector<struct kinfo_proc> procs =
      CF_EXPECT(ReadSysctl<struct kinfo_proc>(mib, sizeof(struct kinfo_proc)));
  CF_EXPECTF(procs.size() == 1, "No process with pid {}", pid);
  return procs.front();
}

Result<std::vector<struct kinfo_proc>> KinfoProcs(const uid_t uid) {
  // The count only caps the copy-out, which the buffer size does as well.
  int mib[] = {CTL_KERN,
               KERN_PROC,
               KERN_PROC_RUID,
               static_cast<int>(uid),
               sizeof(struct kinfo_proc),
               INT_MAX};
  // TODO: Retry if more processes appeared between the size query and the
  // read, which fails with ENOMEM.
  std::vector<struct kinfo_proc> procs =
      CF_EXPECT(ReadSysctl<struct kinfo_proc>(mib));
  // Only report the main thread of each process, like /proc on Linux.
  std::erase_if(procs, [](const struct kinfo_proc& kp) {
    return kp.p_tid != -1 && kp.p_tid != kp.p_pid;
  });
  return procs;
}

// Pass op as KERN_PROC_ARGV or KERN_PROC_ENV for argv or env list
// respectively.
Result<std::vector<std::string>> ProcStrings(const pid_t pid, const int op) {
  int mib[] = {CTL_KERN, KERN_PROC_ARGS, pid, op};
  std::vector<char> buffer = CF_EXPECT(ReadSysctl<char>(mib));
  std::vector<std::string> result;
  for (char** entry = reinterpret_cast<char**>(buffer.data());
       *entry != nullptr; entry++) {
    result.emplace_back(*entry);
  }
  return result;
}

std::unordered_map<std::string, std::string> EnvMap(
    const std::vector<std::string>& envp) {
  std::unordered_map<std::string, std::string> envs;
  for (const std::string& entry : envp) {
    envs.emplace(absl::StrSplit(entry, absl::MaxSplits('=', 1)));
  }
  return envs;
}

// Approximates what execvp(3) resolves: argv[0] against the PATH the process
// was started with, then against its current working directory, which may
// have changed since.
// TODO: Have Command publish the executable it starts in the child's
// environment and prefer that here; on OpenBSD 8.0, we can use
// getexecpath(3), but this result only covers the calling process.
Result<std::string> ResolveArgv0(const pid_t pid, const std::string& argv0) {
  CF_EXPECT(!argv0.empty(), "Empty argv[0]");
  std::string candidate = argv0;
  if (candidate.find('/') == std::string::npos) {
    // KERN_PROC_ENV is the initial environment, the one execvp(3) searched.
    std::string path;
    Result<std::vector<std::string>> envp = ProcStrings(pid, KERN_PROC_ENV);
    if (envp.has_value()) {
      std::unordered_map<std::string, std::string> envs = EnvMap(*envp);
      if (auto it = envs.find("PATH"); it != envs.end()) {
        path = it->second;
      }
    }
    if (!path.empty()) {
      std::vector<std::string> dirs = absl::StrSplit(path, ':');
      candidate = Search(dirs, argv0).value_or(argv0);
    }
  }
  if (candidate.front() != '/') {
    int mib[] = {CTL_KERN, KERN_PROC_CWD, pid};
    char cwd[PATH_MAX] = {};
    size_t size = sizeof(cwd);
    if (sysctl(mib, std::size(mib), cwd, &size, nullptr, 0) == 0 &&
        cwd[0] != '\0') {
      candidate = std::string(cwd) + "/" + candidate;
    }
  }
  std::string resolved;
  if (android::base::Realpath(candidate, &resolved)) {
    return resolved;
  }
  return candidate;
}

}  // namespace

Result<std::vector<pid_t>> CollectPids(const uid_t uid) {
  std::vector<pid_t> pids;
  for (const struct kinfo_proc& kp : CF_EXPECT(KinfoProcs(uid))) {
    pids.push_back(kp.p_pid);
  }
  return pids;
}

Result<std::vector<std::string>> GetCmdArgs(const pid_t pid) {
  uid_t owner = CF_EXPECT(OwnerUid(pid));
  CF_EXPECT_EQ(owner, getuid());
  return CF_EXPECT(ProcStrings(pid, KERN_PROC_ARGV));
}

Result<std::string> GetExecutablePath(const pid_t pid) {
#if OpenBSD >= 202610
  if (pid == getpid()) {
    // OpenBSD 8.0 added getexecpath(3), which returns the realpath(3) of the
    // calling process' executable straight from the kernel.
    char exec_path[PATH_MAX];
    if (getexecpath(exec_path, sizeof(exec_path)) == 0) {
      return std::string(exec_path);
    }
  }
#endif
  // There is no equivalent of /proc/<pid>/exe for arbitrary processes; the
  // best approximation is the argv[0] the process was started with.
  std::vector<std::string> argv = CF_EXPECT(ProcStrings(pid, KERN_PROC_ARGV));
  CF_EXPECTF(!argv.empty(), "Process {} has no arguments", pid);
  return CF_EXPECT(ResolveArgv0(pid, argv.front()));
}

Result<std::vector<pid_t>> CollectPidsByExecName(const std::string& exec_name,
                                                 const uid_t uid) {
  CF_EXPECT_EQ(android::base::Basename(exec_name), exec_name);
  std::vector<pid_t> output_pids;
  for (const struct kinfo_proc& kp : CF_EXPECT(KinfoProcs(uid))) {
    // p_comm is the executable's basename, truncated to _MAXCOMLEN - 1.
    absl::string_view comm(kp.p_comm);
    absl::string_view name(exec_name);
    if (name.size() >= sizeof(kp.p_comm)) {
      name = name.substr(0, sizeof(kp.p_comm) - 1);
    }
    if (comm == name) {
      output_pids.push_back(kp.p_pid);
    }
  }
  return output_pids;
}

Result<uid_t> OwnerUid(const pid_t pid) {
  return CF_EXPECT(KinfoProc(pid)).p_ruid;
}

Result<std::unordered_map<std::string, std::string>> GetEnvs(const pid_t pid) {
  uid_t owner = CF_EXPECT(OwnerUid(pid));
  CF_EXPECT(getuid() == owner, "Owned by another user of uid" << owner);
  return EnvMap(CF_EXPECT(ProcStrings(pid, KERN_PROC_ENV)));
}

Result<ProcInfo> ExtractProcInfo(const pid_t pid) {
  struct kinfo_proc kp = CF_EXPECT(KinfoProc(pid));
  CF_EXPECT_EQ(kp.p_ruid, getuid());
  return ProcInfo{.pid_ = pid,
                  .real_owner_ = kp.p_ruid,
                  .effective_owner_ = kp.p_uid,
                  .actual_exec_path_ = CF_EXPECT(GetExecutablePath(pid)),
                  .envs_ = EnvMap(CF_EXPECT(ProcStrings(pid, KERN_PROC_ENV))),
                  .args_ = CF_EXPECT(ProcStrings(pid, KERN_PROC_ARGV))};
}

Result<pid_t> Ppid(const pid_t pid) { return CF_EXPECT(KinfoProc(pid)).p_ppid; }

}  // namespace cuttlefish
