//
// Copyright (C) 2026 The Android Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <fcntl.h>
#include <signal.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>

#include <chrono>
#include <string>
#include <thread>
#include <unordered_map>

#include "absl/base/log_severity.h"
#include "absl/log/globals.h"
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

#include "cuttlefish/common/libs/fs/fd.h"
#include "cuttlefish/common/libs/utils/files.h"
#include "cuttlefish/files/file_exists.h"
#include "cuttlefish/files/link_or_copy.h"
#include "cuttlefish/files/recursively_remove_directory.h"
#include "cuttlefish/files/write_new_file.h"
#include "cuttlefish/host/commands/cvd/cli/command_request.h"
#include "cuttlefish/host/commands/cvd/cli/commands/start.h"
#include "cuttlefish/host/commands/cvd/cli/selector/selector_common_parser.h"
#include "cuttlefish/host/commands/cvd/instances/cvd_persistent_data.pb.h"
#include "cuttlefish/host/commands/cvd/instances/instance_database.h"
#include "cuttlefish/host/commands/cvd/instances/instance_manager.h"
#include "cuttlefish/host/commands/cvd/instances/local_instance.h"
#include "cuttlefish/host/commands/cvd/instances/local_instance_group.h"
#include "cuttlefish/host/commands/cvd/instances/lock/instance_lock.h"
#include "cuttlefish/posix/readlink.h"
#include "cuttlefish/posix/strerror.h"
#include "cuttlefish/posix/temp_failure_retry.h"  // IWYU pragma: keep
#include "cuttlefish/result/assert.h"
#include "cuttlefish/result/result.h"
#include "cuttlefish/result/result_matchers.h"

namespace cuttlefish {
namespace {

#ifdef __linux__
using ::testing::HasSubstr;

constexpr char kChildRootEnv[] = "CF_START_INTERRUPT_TEST_ROOT";
constexpr std::chrono::seconds kTimeout(10);

class CvdStartInterruptTest : public ::testing::Test {
 protected:
  void SetUp() override { CF_ASSERT(InitEnv()); }

  void TearDown() override {
    if (child_pid_ > 0) {
      kill(-child_pid_, SIGKILL);
      int wstatus = 0;
      TEMP_FAILURE_RETRY(waitpid(child_pid_, &wstatus, 0));
    }
    if (!root_.empty()) {
      (void)RecursivelyRemoveDirectory(root_);
    }
  }

  std::string Path(absl::string_view rel) const {
    return absl::StrCat(root_, "/", rel);
  }

  Result<void> InitEnv() {
    std::string tmpl = absl::StrCat(testing::TempDir(), "/cf_start_int.XXXXXX");
    char* created = mkdtemp(tmpl.data());
    CF_EXPECT(created != nullptr, "mkdtemp failed: " << StrError());
    root_ = created;

    setenv("CUTTLEFISH_METRICS_TRANSMITTER_PATH",
           Path("metrics_transmitter").c_str(), 1);
    setenv("XDG_DATA_HOME", Path("xdg_data").c_str(), 1);
    setenv("ANDROID_PRODUCT_OUT", Path("slot").c_str(), 1);

    for (absl::string_view dir :
         {"slot/bin", "locks/", "per_user",
          "slot/cuttlefish/instances/cvd-1/logs", "slot/cuttlefish_assembly",
          "cuttlefish-common/bin"}) {
      CF_EXPECT(EnsureDirectoryExists(Path(dir)));
    }
    CF_EXPECT(WriteNewFile(Path("locks/local-instance-1.lock"), "", 0666));
    CF_EXPECT(WriteNewFile(
        Path("slot/cuttlefish_assembly/cuttlefish_config.json"), "{}\n"));
    CF_EXPECT(WriteNewFile(
        Path("slot/bin/cvd_internal_start"),
        absl::StrCat(
            "#!/bin/sh\nprintf 'VIRTUAL_DEVICE_BOOT_COMPLETED\\n' >> '",
            Path("slot/cuttlefish/instances/cvd-1/logs/launcher.log"), "'\n"),
        0755));
    CF_EXPECT(WriteNewFile(
        Path("slot/bin/cvd_internal_stop"),
        absl::StrCat("#!/bin/sh\ntouch '", Path("stop_started"),
                     "'\ni=0\nwhile [ ! -f '", Path("stop_release"),
                     "' ] && [ \"$i\" -lt 1500 ]; do sleep 0.01; i=$((i+1)); "
                     "done\ntouch '",
                     Path("stop_completed"), "'\n"),
        0755));

    // HostPackageSubstitution requires .../cuttlefish-common/bin/cvd, and
    // hosts without /dev/kvm wrap launches with cvd_refresh_groups beside it.
    CF_EXPECT(WriteNewFile(Path("cuttlefish-common/bin/cvd_refresh_groups"),
                           "#!/bin/sh\nbin=$1\nshift 2\nexec \"$bin\" \"$@\"\n",
                           0755));
    const std::string self_exe = CF_EXPECT(ReadLink("/proc/self/exe"));
    CF_EXPECT(LinkOrCopy(self_exe, Path("cuttlefish-common/bin/cvd")));

    InstanceLockFileManager lock_manager(Path("locks/"));
    InstanceDatabase instance_db(Path("instance_database.binpb"));
    InstanceManager manager(&lock_manager, instance_db, Path("per_user"));
    LocalInstanceGroup group = CF_EXPECT(manager.CreateInstanceGroup(
        InstanceGroupParams{
            .group_name = "cvd_1",
            .instances = {InstanceParams{.instance_id = 1,
                                         .per_instance_name = "1"}},
        },
        InstanceManager::GroupDirectories{
            .home = Path("slot"),
            .host_artifacts_path = Path("slot"),
            .product_out_paths = {Path("slot")},
        }));
    group.SetAllStates(cvd::INSTANCE_STATE_STOPPED);
    CF_EXPECT(manager.UpdateInstanceGroup(group));
    return {};
  }

  void SpawnPty() {
    const int raw_pty = posix_openpt(O_RDWR | O_NOCTTY | O_NONBLOCK);
    ASSERT_GE(raw_pty, 0) << StrError();
    ASSERT_EQ(grantpt(raw_pty), 0) << StrError();
    ASSERT_EQ(unlockpt(raw_pty), 0) << StrError();
    const char* slave_name = ptsname(raw_pty);
    ASSERT_NE(slave_name, nullptr) << StrError();
    const std::string slave_path(slave_name);
    pty_fd_ = CF_ASSERT(Fd::Dup(raw_pty));
    close(raw_pty);

    const std::string cvd_exe = Path("cuttlefish-common/bin/cvd");
    child_pid_ = fork();
    ASSERT_GE(child_pid_, 0) << StrError();
    if (child_pid_ == 0) {
      pty_fd_.Close();
      if (setsid() < 0) {
        _exit(20);
      }
      const int slave_fd = open(slave_path.c_str(), O_RDWR);
      if (slave_fd < 0) {
        _exit(21);
      }
      // NOLINTNEXTLINE(misc-include-cleaner): <sys/ioctl.h>
      (void)ioctl(slave_fd, TIOCSCTTY, 0);
      struct termios attrs = {};
      if (tcgetattr(slave_fd, &attrs) == 0) {
        attrs.c_lflag |= (ISIG | ICANON | ECHO | NOFLSH);
        attrs.c_cc[VINTR] = '\x03';
        (void)tcsetattr(slave_fd, TCSANOW, &attrs);
      }
      struct winsize ws = {.ws_row = 40, .ws_col = 120};
      // NOLINTNEXTLINE(misc-include-cleaner): <sys/ioctl.h>
      (void)ioctl(slave_fd, TIOCSWINSZ, &ws);
      for (int fd : {STDIN_FILENO, STDOUT_FILENO, STDERR_FILENO}) {
        dup2(slave_fd, fd);
      }
      if (slave_fd > STDERR_FILENO) {
        close(slave_fd);
      }
      unsetenv("TEST_PREMATURE_EXIT_FILE");
      unsetenv("XML_OUTPUT_FILE");
      setenv(kChildRootEnv, root_.c_str(), 1);
      setenv("CVD_IN_SANDBOX", "1", 1);
      char* argv[] = {
          const_cast<char*>(cvd_exe.c_str()),
          const_cast<char*>("--gtest_also_run_disabled_tests"),
          const_cast<char*>(
              "--gtest_filter=CvdStartInterruptChild.DISABLED_Entrypoint"),
          nullptr,
      };
      execv(cvd_exe.c_str(), argv);
      _exit(22);
    }
  }

  void DrainPty() {
    char buf[4096];
    while (true) {
      const Result<uint64_t, int> n = pty_fd_.Read(buf, sizeof(buf));
      if (!n.has_value() || *n == 0) {
        break;
      }
      output_.append(buf, static_cast<size_t>(*n));
    }
  }

  Result<cvd::InstanceState> StoredState() const {
    const InstanceDatabase instance_db(Path("instance_database.binpb"));
    const LocalInstanceGroup group =
        CF_EXPECT(instance_db.FindGroup({.group_name = "cvd_1"}));
    CF_EXPECT_EQ(group.Instances().size(), 1u);
    return group.Instances()[0].State();
  }

  template <typename Pred>
  void WaitWhileDraining(Pred&& pred, absl::string_view label,
                         bool fail_on_exit = true) {
    const std::chrono::steady_clock::time_point deadline =
        std::chrono::steady_clock::now() + kTimeout;
    while (std::chrono::steady_clock::now() < deadline) {
      DrainPty();
      if (pred()) {
        return;
      }
      int status = 0;
      if (fail_on_exit && waitpid(child_pid_, &status, WNOHANG) == child_pid_) {
        child_pid_ = -1;
        FAIL() << "Child exited before " << label << " (wstatus=" << status
               << "). Output:\n"
               << output_;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    FAIL() << "Timed out waiting for " << label << ". Output:\n" << output_;
  }

  void WaitUntilRunning() {
    WaitWhileDraining(
        [this]() {
          const Result<cvd::InstanceState> state = StoredState();
          return absl::StrContains(output_, "\x1b[?1049h") &&
                 state.has_value() && *state == cvd::INSTANCE_STATE_RUNNING;
        },
        "RUNNING + alternate screen");
  }

  void WaitForFile(const std::string& path) {
    WaitWhileDraining([&path]() { return FileExists(path); }, path);
  }

  void SendCtrlC() {
    const char ctrl_c = '\x03';
    ASSERT_EQ(CF_ASSERT(pty_fd_.Write(&ctrl_c, 1)), 1u);
  }

  void ExpectStoppedCleanly() {
    int wstatus = 0;
    ASSERT_NO_FATAL_FAILURE(WaitWhileDraining(
        [this, &wstatus]() {
          return waitpid(child_pid_, &wstatus, WNOHANG) == child_pid_;
        },
        "child exit", /*fail_on_exit=*/false));
    child_pid_ = -1;
    DrainPty();
    ASSERT_TRUE(WIFEXITED(wstatus))
        << "Child died from signal " << WTERMSIG(wstatus) << ". Output:\n"
        << output_;
    EXPECT_EQ(WEXITSTATUS(wstatus), 0) << "Output:\n" << output_;
    EXPECT_TRUE(FileExists(Path("stop_completed")));
    EXPECT_THAT(StoredState(), IsOkAndValue(cvd::INSTANCE_STATE_STOPPED));
    EXPECT_THAT(output_, HasSubstr("Device stopped."));
    EXPECT_THAT(output_, HasSubstr("\x1b[?1049l"));
  }

  std::string root_;
  Fd pty_fd_;
  pid_t child_pid_ = -1;
  std::string output_;
};

// Re-exec'd child entrypoint invoked from <tmp>/cuttlefish-common/bin/cvd.
TEST(CvdStartInterruptChild, DISABLED_Entrypoint) {
  absl::SetStderrThreshold(absl::LogSeverityAtLeast::kInfo);
  const char* root_env = getenv(kChildRootEnv);
  ASSERT_NE(root_env, nullptr);
  const std::string root(root_env);
  const std::string slot_dir = absl::StrCat(root, "/slot");

  InstanceLockFileManager lock_manager(absl::StrCat(root, "/locks/"));
  InstanceDatabase instance_db(absl::StrCat(root, "/instance_database.binpb"));
  InstanceManager manager(&lock_manager, instance_db,
                          absl::StrCat(root, "/per_user"));

  const std::unordered_map<std::string, std::string> env = {
      {"HOME", slot_dir},
      {"ANDROID_HOST_OUT", slot_dir},
      {"ANDROID_PRODUCT_OUT", slot_dir},
      {"PATH", "/usr/bin:/bin"},
      {"TERM", "xterm-256color"},
  };
  CommandRequest request = CF_ASSERT(
      CommandRequestBuilder()
          .SetEnv(env)
          .AddArguments({"cvd", "start", "--daemon=false"})
          .SetSelectorOptions(selector::SelectorOptions{.group_name = "cvd_1"})
          .Build());

  CvdStartCommandHandler handler(manager);
  CF_ASSERT(handler.Handle(request));
}

TEST_F(CvdStartInterruptTest, DoubleCtrlCDuringShutdownRecordsStopped) {
  SpawnPty();
  WaitUntilRunning();

  SendCtrlC();
  WaitForFile(Path("stop_started"));

  SendCtrlC();
  std::this_thread::sleep_for(std::chrono::milliseconds(20));
  SendCtrlC();
  WaitWhileDraining(
      [this]() {
        return absl::StrContains(output_,
                                 "Already stopping device, please wait...");
      },
      "already stopping message");
  CF_ASSERT(WriteNewFile(Path("stop_release"), "go"));

  ExpectStoppedCleanly();
}

TEST_F(CvdStartInterruptTest, RapidCtrlCBurstBeforeStopStartsRecordsStopped) {
  SpawnPty();
  WaitUntilRunning();

  for (int i = 0; i < 5; ++i) {
    SendCtrlC();
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  WaitForFile(Path("stop_started"));
  CF_ASSERT(WriteNewFile(Path("stop_release"), "go"));

  ExpectStoppedCleanly();
}

TEST_F(CvdStartInterruptTest,
       SingleCtrlCDuringBootCompleteMetricsStopsCleanly) {
  // Succeed on GatherVmStartMetrics (1st call), then pause inside
  // GatherVmBootCompleteMetrics (2nd call) so Ctrl+C arrives mid-metrics.
  // Use shell redirection (no child fork and no trap) so SIGINT stays SIG_DFL.
  const std::string transmitter = absl::StrCat(
      "#!/bin/sh\nif [ ! -f '", Path("metrics_called"), "' ]; then : > '",
      Path("metrics_called"), "'; exit 0; fi\n: > '", Path("metrics_started"),
      "'\nexec sleep 30\n");
  CF_ASSERT(WriteNewFile(Path("metrics_transmitter"), transmitter, 0755));

  SpawnPty();
  WaitForFile(Path("metrics_started"));

  SendCtrlC();
  WaitForFile(Path("stop_started"));

  SendCtrlC();
  std::this_thread::sleep_for(std::chrono::milliseconds(20));
  CF_ASSERT(WriteNewFile(Path("stop_release"), "go"));

  ExpectStoppedCleanly();
}
#endif

}  // namespace
}  // namespace cuttlefish
