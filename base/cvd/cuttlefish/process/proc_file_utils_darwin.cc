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
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "absl/strings/str_split.h"
#include "absl/strings/string_view.h"
#include "android-base/file.h"

#include "cuttlefish/posix/strerror.h"
#include "cuttlefish/process/proc_file_utils.h"
#include "cuttlefish/process/proc_file_utils_sysctl.h"
#include "cuttlefish/result/result.h"

namespace cuttlefish {
namespace {

Result<struct kinfo_proc> KinfoProc(const pid_t pid) {
  int mib[] = {CTL_KERN, KERN_PROC, KERN_PROC_PID, pid};
  std::vector<struct kinfo_proc> procs =
      CF_EXPECT(ReadSysctl<struct kinfo_proc>(mib, sizeof(struct kinfo_proc)));
  CF_EXPECTF(procs.size() == 1, "No process with pid {}", pid);
  return procs.front();
}

Result<std::vector<struct kinfo_proc>> KinfoProcs(const uid_t uid) {
  int mib[] = {CTL_KERN, KERN_PROC, KERN_PROC_RUID, static_cast<int>(uid)};
  // TODO: Retry if more processes appeared between the size query and the
  // read, which fails with ENOMEM.
  return CF_EXPECT(ReadSysctl<struct kinfo_proc>(mib));
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
  std::vector<int> argmax = CF_EXPECT(ReadSysctl<int>(argmax_mib, sizeof(int)));
  CF_EXPECT_EQ(argmax.size(), 1u);
  int mib[] = {CTL_KERN, KERN_PROCARGS2, pid};
  std::vector<char> buffer = CF_EXPECT(ReadSysctl<char>(mib, argmax.front()));
  CF_EXPECTF(buffer.size() >= sizeof(int),
             "kern.procargs2.{} returned {} bytes", pid, buffer.size());
  int argc = 0;
  std::memcpy(&argc, buffer.data(), sizeof(argc));

  // Empty strings are legal argv elements, so only the padding is skipped.
  std::vector<absl::string_view> parts =
      absl::StrSplit(absl::string_view(buffer.data() + sizeof(argc),
                                      buffer.size() - sizeof(argc)),
                     '\0');
  auto it = parts.begin();
  CF_EXPECTF(it != parts.end(), "kern.procargs2.{} ended early", pid);
  it++;  // The executable path.
  while (it != parts.end() && it->empty()) {
    it++;
  }
  ProcStrings strings;
  for (int i = 0; i < argc; i++) {
    CF_EXPECTF(it != parts.end(), "kern.procargs2.{} ended early", pid);
    strings.argv.emplace_back(*it++);
  }
  while (it != parts.end() && !it->empty()) {
    strings.envp.emplace_back(*it++);
  }
  return strings;
}

std::unordered_map<std::string, std::string> EnvMap(
    const std::vector<std::string>& envp) {
  std::unordered_map<std::string, std::string> envs;
  for (const std::string& entry : envp) {
    envs.emplace(absl::StrSplit(entry, absl::MaxSplits('=', 1)));
  }
  return envs;
}

}  // namespace

Result<std::vector<pid_t>> CollectPids(const uid_t uid) {
  std::vector<pid_t> pids;
  for (const struct kinfo_proc& kp : CF_EXPECT(KinfoProcs(uid))) {
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
  for (const struct kinfo_proc& kp : CF_EXPECT(KinfoProcs(uid))) {
    // p_comm is the executable's basename, truncated to MAXCOMLEN.
    absl::string_view comm(kp.kp_proc.p_comm);
    absl::string_view name(exec_name);
    if (name.size() >= sizeof(kp.kp_proc.p_comm)) {
      name = name.substr(0, sizeof(kp.kp_proc.p_comm) - 1);
    }
    if (comm == name) {
      output_pids.push_back(kp.kp_proc.p_pid);
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
  return EnvMap(CF_EXPECT(GetProcStrings(pid)).envp);
}

Result<ProcInfo> ExtractProcInfo(const pid_t pid) {
  struct kinfo_proc kp = CF_EXPECT(KinfoProc(pid));
  CF_EXPECT_EQ(kp.kp_eproc.e_pcred.p_ruid, getuid());
  ProcStrings strings = CF_EXPECT(GetProcStrings(pid));
  return ProcInfo{.pid_ = pid,
                  .real_owner_ = kp.kp_eproc.e_pcred.p_ruid,
                  .effective_owner_ = kp.kp_eproc.e_ucred.cr_uid,
                  .actual_exec_path_ = CF_EXPECT(GetExecutablePath(pid)),
                  .envs_ = EnvMap(strings.envp),
                  .args_ = std::move(strings.argv)};
}

Result<pid_t> Ppid(const pid_t pid) {
  return CF_EXPECT(KinfoProc(pid)).kp_eproc.e_ppid;
}

}  // namespace cuttlefish
