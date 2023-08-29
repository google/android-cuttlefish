/*
 * Copyright (C) 2023 The Android Open Source Project
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

#include "host/commands/cvd/handle_reset.h"

#include <errno.h>
#include <semaphore.h>
#include <sys/mman.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <unistd.h>

#include <chrono>
#include <iostream>
#include <optional>
#include <string>
#include <thread>

#include <android-base/logging.h>

#include "common/libs/utils/flag_parser.h"
#include "common/libs/utils/subprocess.h"
#include "host/commands/cvd/common_utils.h"
#include "host/commands/cvd/reset_client_utils.h"

namespace cuttlefish {

struct ParsedFlags {
  bool is_help;
  bool clean_runtime_dir;
  bool device_by_cvd_only;
  bool is_confirmed_by_flag;
  std::optional<android::base::LogSeverity> log_level;
};

static Result<ParsedFlags> ParseResetFlags(cvd_common::Args subcmd_args) {
  if (subcmd_args.size() > 2 && subcmd_args.at(2) == "help") {
    // unfortunately, {FlagAliasMode::kFlagExact, "help"} is not allowed
    subcmd_args[2] = "--help";
  }

  bool is_help = false;
  bool clean_runtime_dir = true;
  bool device_by_cvd_only = false;
  bool is_confirmed_by_flag = false;
  std::string verbosity_flag_value;

  Flag y_flag =
      Flag()
          .Alias({FlagAliasMode::kFlagExact, "-y"})
          .Alias({FlagAliasMode::kFlagExact, "--yes"})
          .Setter([&is_confirmed_by_flag](const FlagMatch&) -> Result<void> {
            is_confirmed_by_flag = true;
            return {};
          });
  Flag help_flag = Flag()
                       .Alias({FlagAliasMode::kFlagExact, "-h"})
                       .Alias({FlagAliasMode::kFlagExact, "--help"})
                       .Setter([&is_help](const FlagMatch&) -> Result<void> {
                         is_help = true;
                         return {};
                       });
  std::vector<Flag> flags{
      GflagsCompatFlag("device-by-cvd-only", device_by_cvd_only),
      y_flag,
      GflagsCompatFlag("clean-runtime-dir", clean_runtime_dir),
      help_flag,
      GflagsCompatFlag("verbosity", verbosity_flag_value),
      UnexpectedArgumentGuard()};
  CF_EXPECT(ParseFlags(flags, subcmd_args));

  std::optional<android::base::LogSeverity> verbosity;
  if (!verbosity_flag_value.empty()) {
    verbosity = CF_EXPECT(EncodeVerbosity(verbosity_flag_value),
                          "invalid verbosity level");
  }
  return ParsedFlags{.is_help = is_help,
                     .clean_runtime_dir = clean_runtime_dir,
                     .device_by_cvd_only = device_by_cvd_only,
                     .is_confirmed_by_flag = is_confirmed_by_flag,
                     .log_level = verbosity};
}

static bool GetUserConfirm() {
  std::cout << "Are you sure to reset all the devices, runtime files, "
            << "and the cvd server if any [y/n]? ";
  std::string user_confirm;
  std::getline(std::cin, user_confirm);
  std::transform(user_confirm.begin(), user_confirm.end(), user_confirm.begin(),
                 ::tolower);
  return (user_confirm == "y" || user_confirm == "yes");
}

/*
 * Try client.StopCvdServer(), and wait for a while.
 *
 * There should be two threads or processes. One is to call
 * "StopCvdServer()," which could hang forever. The other is waiting
 * for the thread/process, and should kill it after timeout.
 *
 * In that sense, a process is easy to kill in the middle (kill -9).
 *
 */
static Result<void> TimedKillCvdServer(CvdClient& client, const int timeout) {
  sem_t* binary_sem = (sem_t*)mmap(NULL, sizeof(sem_t), PROT_READ | PROT_WRITE,
                                   MAP_ANONYMOUS | MAP_SHARED, 0, 0);
  CF_EXPECT(binary_sem != nullptr,
            "Failed to allocated shm for inter-process semaphore."
                << "(errno: " << errno << ")");
  CF_EXPECT_EQ(sem_init(binary_sem, 1, 0), 0,
               "Failed to initialized inter-process semaphore"
                   << "(errno: " << errno << ")");
  pid_t pid = fork();
  CF_EXPECT(pid >= 0, "fork() failed in TimedKillCvdServer");
  if (pid == 0) {
    LOG(ERROR) << "Stopping the cvd server...";
    constexpr bool clear_running_devices_first = true;
    auto stop_server_result = client.StopCvdServer(clear_running_devices_first);
    if (!stop_server_result.ok()) {
      LOG(ERROR) << "cvd kill-server returned error"
                 << stop_server_result.error().FormatForEnv();
      LOG(ERROR) << "However, cvd reset will continue cleaning up.";
    }
    sem_post(binary_sem);
    // exit 0. This is a short-living worker process
    exit(0);
  }

  Subprocess worker_process(pid);
  struct timespec waiting_time;
  if (clock_gettime(CLOCK_MONOTONIC, &waiting_time) == -1) {
    // cannot set up an alarm clock. Not sure how long it should wait
    // for the worker process. Thus, we wait for a certain amount of time,
    // and send SIGKILL to the cvd server process and the worker process.
    LOG(ERROR) << "Could not get the CLOCK_REALTIME.";
    LOG(ERROR) << "Sleeping " << timeout << " seconds, and "
               << "will send sigkill to the server.";
    using namespace std::chrono_literals;
    std::this_thread::sleep_for(operator""s((unsigned long long)timeout));
    auto result_kill = KillCvdServerProcess();
    worker_process.Stop();
    // TODO(kwstephenkim): Compose error messages, and propagate
    CF_EXPECT(result_kill.ok(), "KillCvdServerProcess() failed.");
    return {};
  }

  // timed wait for the binary semaphore
  waiting_time.tv_sec += timeout;
  auto ret_code = sem_timedwait(binary_sem, &waiting_time);

  // ret_code == 0 means sem_wait succeeded before timeout.
  if (ret_code == 0) {
    worker_process.Wait();
    CF_EXPECT(KillCvdServerProcess());
    return {};
  }

  // worker process is still running.
  worker_process.Stop();
  CF_EXPECT(KillCvdServerProcess());
  return {};
}

Result<void> HandleReset(CvdClient& client,
                         const cvd_common::Args& subcmd_args) {
  auto options = CF_EXPECT(ParseResetFlags(subcmd_args));
  if (options.log_level) {
    SetMinimumVerbosity(options.log_level.value());
  }
  if (options.is_help) {
    std::cout << kHelpMessage << std::endl;
    return {};
  }

  // cvd reset. Give one more opportunity
  if (!options.is_confirmed_by_flag && !GetUserConfirm()) {
    std::cout << "For more details: "
              << "  cvd reset --help" << std::endl;
    return {};
  }

  auto result = TimedKillCvdServer(client, 50);
  if (!result.ok()) {
    LOG(ERROR) << result.error().FormatForEnv();
    LOG(ERROR) << "Cvd reset continues cleaning up devices.";
  }
  // cvd reset handler placeholder. identical to cvd kill-server for now.
  CF_EXPECT(KillAllCuttlefishInstances(
      {.cvd_server_children_only = options.device_by_cvd_only,
       .clear_instance_dirs = options.clean_runtime_dir}));
  return {};
}

}  // namespace cuttlefish
