/*
 * Copyright (C) 2021 The Android Open Source Project
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

#include "cuttlefish/host/commands/run_cvd/daemonize_launcher.h"

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cstdlib>

#include "absl/log/log.h"

#include "cuttlefish/common/libs/fs/fd.h"
#include "cuttlefish/common/libs/fs/shared_fd.h"
#include "cuttlefish/common/libs/utils/tee_logging.h"
#include "cuttlefish/host/commands/run_cvd/wattson_rebalance_threads.h"
#include "cuttlefish/host/libs/command_util/runner/defs.h"
#include "cuttlefish/host/libs/config/config_constants.h"
#include "cuttlefish/host/libs/config/cuttlefish_config.h"
#include "cuttlefish/posix/strerror.h"
#include "cuttlefish/result/expect.h"
#include "cuttlefish/result/result_type.h"

namespace cuttlefish {

// Forks run_cvd into a daemonized child process. The current process continues
// only until the child has signalled that the boot is finished.
//
// `DaemonizeLauncher` returns the write end of a pipe. The child is expected
// to write a `RunnerExitCodes` into the pipe when the boot finishes.
Result<SharedFD> DaemonizeLauncher(const CuttlefishConfig& config) {
  auto instance = config.ForDefaultInstance();
  SharedFD read_end, write_end;
  CF_EXPECT(SharedFD::Pipe(&read_end, &write_end), "Unable to create pipe");
  auto pid = fork();
  if (pid) {
    // Explicitly close here, otherwise we may end up reading forever if the
    // child process dies.
    write_end->Close();
    RunnerExitCodes exit_code;
    uint64_t bytes_read =
        read_end->Read(&exit_code, sizeof(exit_code)).value_or(0);
    if (bytes_read != sizeof(exit_code)) {
      LOG(ERROR) << "Failed to read a complete exit code, read " << bytes_read
                 << " bytes only instead of the expected " << sizeof(exit_code);
      exit_code = RunnerExitCodes::kPipeIOError;
    } else if (exit_code == RunnerExitCodes::kSuccess) {
      if (IsRestoring(config)) {
        LOG(INFO) << "Virtual device restored successfully";
      } else {
        LOG(INFO) << "Virtual device booted successfully";
        if (!instance.vcpu_config_path().empty()) {
          CF_EXPECT(WattsonRebalanceThreads(instance.id()));
        }
      }
    } else if (exit_code == RunnerExitCodes::kVirtualDeviceBootFailed) {
      if (IsRestoring(config)) {
        LOG(ERROR) << "Virtual device failed to restore";
      } else {
        LOG(ERROR) << "Virtual device failed to boot";
      }
      if (!instance.fail_fast()) {
        LOG(ERROR) << "Device has been left running for debug";
      }
    } else {
      LOG(ERROR) << "Unexpected exit code: " << exit_code;
    }
    if (!IsRestoring(config)) {
      if (exit_code == RunnerExitCodes::kSuccess) {
        VLOG(0) << kBootCompletedMessage;
      } else {
        LOG(ERROR) << kBootFailedMessage;
      }
    }
    std::exit(exit_code);
  } else {
    // The child returns the write end of the pipe
    if (daemon(/*nochdir*/ 1, /*noclose*/ 1) != 0) {
      LOG(ERROR) << "Failed to daemonize child process: " << StrError(errno);
      std::exit(RunnerExitCodes::kDaemonizationError);
    }
    // Redirect standard I/O
    auto log_path = instance.launcher_log_path();
    SharedFD log = Fd::Open(log_path, O_CREAT | O_WRONLY | O_APPEND,
                            S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP)
                       .value_or(Fd());
    if (!log->IsOpen()) {
      LOG(ERROR) << "Failed to create launcher log file: " << log->StrError();
      std::exit(RunnerExitCodes::kDaemonizationError);
    }
    SetLoggers(
        {SeverityTarget::FromFd(log, MetadataLevel::FULL, LogFileSeverity())});
    Result<Fd> dev_null = Fd::Open("/dev/null", O_RDONLY);
    if (!dev_null.has_value()) {
      LOG(ERROR) << "Failed to open /dev/null: " << dev_null.error();
      std::exit(RunnerExitCodes::kDaemonizationError);
    }
    if (dev_null->UNMANAGED_Dup2(0) < 0) {
      LOG(ERROR) << "Failed dup2 stdin: " << dev_null->StrError();
      std::exit(RunnerExitCodes::kDaemonizationError);
    }
    if (log->UNMANAGED_Dup2(1) < 0) {
      LOG(ERROR) << "Failed dup2 stdout: " << log->StrError();
      std::exit(RunnerExitCodes::kDaemonizationError);
    }
    if (log->UNMANAGED_Dup2(2) < 0) {
      LOG(ERROR) << "Failed dup2 seterr: " << log->StrError();
      std::exit(RunnerExitCodes::kDaemonizationError);
    }

    read_end->Close();
    return write_end;
  }
}

}  // namespace cuttlefish
