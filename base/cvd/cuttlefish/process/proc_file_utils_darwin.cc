/*
 * Copyright (C) 2025 The Android Open Source Project
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

#include <errno.h>
#include <libproc.h>
#include <sys/sysctl.h>
#include <sys/types.h>
#include <unistd.h>

#include <cstring>
#include <iterator>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "absl/log/log.h"
#include "absl/strings/str_split.h"
#include "android-base/file.h"

#include "cuttlefish/posix/strerror.h"
#include "cuttlefish/process/proc_file_utils.h"
#include "cuttlefish/result/result.h"

namespace cuttlefish {
namespace {

Result<struct kinfo_proc> KinfoProc(const pid_t pid) {
  struct kinfo_proc kp = {};
  size_t size = sizeof(kp);
  int mib[] = {CTL_KERN, KERN_PROC, KERN_PROC_PID, pid};
  CF_EXPECTF(sysctl(mib, std::size(mib), &kp, &size, nullptr, 0) == 0,
             "sysctl(kern.proc.pid.{}) failed: {}", pid, StrError(errno));
  CF_EXPECTF(size == sizeof(kp), "No process with pid {}", pid);
  return kp;
}

struct ProcStrings {
  std::vector<std::string> argv;
  std::vector<std::string> envp;
};

// KERN_PROCARGS2 returns one buffer laid out as: an int argc, the executable
// path, NUL padding, the argc argv strings, then the environment strings up to
// an empty string. Everything is NUL-terminated.
Result<ProcStrings> GetProcStrings(const pid_t pid) {
  int argmax_mib[] = {CTL_KERN, KERN_ARGMAX};
  int argmax = 0;
  size_t size = sizeof(argmax);
  CF_EXPECTF(sysctl(argmax_mib, std::size(argmax_mib), &argmax, &size, nullptr,
                    0) == 0,
             "sysctl(kern.argmax) failed: {}", StrError(errno));

  int mib[] = {CTL_KERN, KERN_PROCARGS2, pid};
  std::vector<char> buffer(argmax);
  size = buffer.size();
  CF_EXPECTF(sysctl(mib, std::size(mib), buffer.data(), &size, nullptr, 0) == 0,
             "sysctl(kern.procargs2.{}) failed: {}", pid, StrError(errno));
  buffer.resize(size);

  CF_EXPECTF(size >= sizeof(int), "kern.procargs2.{} returned {} bytes", pid,
             size);
  int argc = 0;
  std::memcpy(&argc, buffer.data(), sizeof(argc));
  const char* cursor = buffer.data() + sizeof(argc);
  const char* const end = buffer.data() + buffer.size();

  auto next_string = [&]() -> Result<std::string_view> {
    CF_EXPECTF(cursor < end, "kern.procargs2.{} ended early", pid);
    std::string_view str(cursor, strnlen(cursor, end - cursor));
    cursor += str.size() + 1;
    return str;
  };

  CF_EXPECT(next_string());  // The executable path.
  while (cursor < end && *cursor == '\0') {
    cursor++;
  }
  ProcStrings strings;
  for (int i = 0; i < argc; i++) {
    strings.argv.emplace_back(CF_EXPECT(next_string()));
  }
  while (cursor < end && *cursor != '\0') {
    strings.envp.emplace_back(CF_EXPECT(next_string()));
  }
  return strings;
}

}  // namespace

Result<std::vector<pid_t>> CollectPids(const uid_t uid) {
  int mib[] = {CTL_KERN, KERN_PROC, KERN_PROC_RUID, static_cast<int>(uid)};
  size_t size = 0;
  CF_EXPECTF(sysctl(mib, std::size(mib), nullptr, &size, nullptr, 0) == 0,
             "sysctl(kern.proc.ruid.{}) failed: {}", uid, StrError(errno));
  std::vector<struct kinfo_proc> procs(size / sizeof(struct kinfo_proc));
  size = procs.size() * sizeof(struct kinfo_proc);
  // TODO: Retry if more than that appeared and the second call fails with
  // ENOMEM.
  CF_EXPECTF(sysctl(mib, std::size(mib), procs.data(), &size, nullptr, 0) == 0,
             "sysctl(kern.proc.ruid.{}) failed: {}", uid, StrError(errno));
  procs.resize(size / sizeof(struct kinfo_proc));
  std::vector<pid_t> pids;
  for (const struct kinfo_proc& kp : procs) {
    pids.push_back(kp.kp_proc.p_pid);
  }
  return pids;
}

Result<std::vector<std::string>> GetCmdArgs(const pid_t pid) {
  uid_t owner = CF_EXPECT(OwnerUid(pid));
  CF_EXPECT_EQ(owner, getuid());
  return CF_EXPECT(GetProcStrings(pid)).argv;
}

Result<std::string> GetExecutablePath(const pid_t pid) {
  char path[PROC_PIDPATHINFO_MAXSIZE] = {};
  CF_EXPECTF(proc_pidpath(pid, path, sizeof(path)) > 0,
             "proc_pidpath({}) failed: {}", pid, StrError(errno));
  return std::string(path);
}

Result<std::vector<pid_t>> CollectPidsByExecName(const std::string& exec_name,
                                                 const uid_t uid) {
  CF_EXPECT_EQ(android::base::Basename(exec_name), exec_name);
  std::vector<pid_t> output_pids;
  for (const pid_t pid : CF_EXPECT(CollectPids(uid))) {
    Result<struct kinfo_proc> kp = KinfoProc(pid);
    if (!kp.has_value() || kp->kp_eproc.e_pcred.p_ruid != uid) {
      VLOG(1) << "Process #" << pid << " does not belong to " << uid;
      continue;
    }
    // p_comm is the executable's basename, truncated to MAXCOMLEN.
    std::string_view comm(kp->kp_proc.p_comm);
    std::string_view name(exec_name);
    if (name.size() >= sizeof(kp->kp_proc.p_comm)) {
      name = name.substr(0, sizeof(kp->kp_proc.p_comm) - 1);
    }
    if (comm == name) {
      output_pids.push_back(pid);
    }
  }
  return output_pids;
}

Result<uid_t> OwnerUid(const pid_t pid) {
  return CF_EXPECT(KinfoProc(pid)).kp_eproc.e_pcred.p_ruid;
}

Result<std::unordered_map<std::string, std::string>> GetEnvs(const pid_t pid) {
  uid_t owner = CF_EXPECT(OwnerUid(pid));
  CF_EXPECT(getuid() == owner, "Owned by another user of uid" << owner);
  std::unordered_map<std::string, std::string> envs;
  for (const std::string& entry : CF_EXPECT(GetProcStrings(pid)).envp) {
    envs.emplace(absl::StrSplit(entry, absl::MaxSplits('=', 1)));
  }
  return envs;
}

Result<ProcInfo> ExtractProcInfo(const pid_t pid) {
  struct kinfo_proc kp = CF_EXPECT(KinfoProc(pid));
  return ProcInfo{.pid_ = pid,
                  .real_owner_ = kp.kp_eproc.e_pcred.p_ruid,
                  .effective_owner_ = kp.kp_eproc.e_ucred.cr_uid,
                  .actual_exec_path_ = CF_EXPECT(GetExecutablePath(pid)),
                  .envs_ = CF_EXPECT(GetEnvs(pid)),
                  .args_ = CF_EXPECT(GetCmdArgs(pid))};
}

Result<pid_t> Ppid(const pid_t pid) {
  return CF_EXPECT(KinfoProc(pid)).kp_eproc.e_ppid;
}

}  // namespace cuttlefish
