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

#include "cuttlefish/metrics/guest/boot_failure.h"

#include "gtest/gtest.h"

namespace cuttlefish {
namespace {

TEST(BootFailureTest, EmptyLogsReturnsUnspecified) {
  const BootFailureLogs logs{};
  EXPECT_EQ(CategorizeBootFailure(logs), BootFailureCategory::kUnspecified);
}

TEST(BootFailureTest, BootCompletedReturnsUnspecified) {
  const BootFailureLogs logs{
      .launcher_log =
          "run_cvd 04-20 06:58:10.312 630 630 kernel_log_monitor.cpp:173] "
          "GUEST_UBOOT_VERSION: 2021.10\n"
          "run_cvd 04-20 06:58:12.000 630 630 kernel_log_monitor.cpp:181] "
          "GUEST_KERNEL_VERSION: 6.18.32\n"
          "run_cvd 04-20 06:58:26.863 630 633 monitor_utils.cpp:74] "
          "Virtual device booted successfully\n"
          "VIRTUAL_DEVICE_BOOT_COMPLETED\n",
  };
  EXPECT_EQ(CategorizeBootFailure(logs), BootFailureCategory::kUnspecified);
}

TEST(BootFailureTest, AssemblyFailureWhenRunCvdNeverStarts) {
  const BootFailureLogs logs{
      .assemble_log =
          "assemble_cvd E 04-20 06:58:00.000 500 500 assemble_cvd.cc:200] "
          "Failed to initialize instance composite disk\n",
      .launcher_log =
          "assemble_cvd E 04-20 06:58:00.000 500 500 assemble_cvd.cc:200] "
          "Failed to initialize instance composite disk\n",
  };
  EXPECT_EQ(CategorizeBootFailure(logs), BootFailureCategory::kAssemblyFailure);
}

TEST(BootFailureTest, VmmFailureOnCrosvmError) {
  const BootFailureLogs logs{
      .launcher_log =
          "Starting monitored subprocess /tmp/cf/bin/process_restarter\n"
          "[2026-04-20T05:13:51.997704929+00:00 ERROR crosvm] exiting with "
          "error 1: failed to connect to vhost-user socket path "
          "/tmp/cf_avd_104/cvd-1/internal/vhost_user_mac80211\n"
          "process_restarter E 04-20 05:13:52.004 1096 1096 "
          "process_restarter.cc:135] crosvm exited with exit code: 1\n"
          "run_cvd E 04-20 05:13:52.005 638 696 process_monitor.cc:184] "
          "Detected unexpected exit of monitored subprocess "
          "/tmp/cf/bin/process_restarter\n"
          "VIRTUAL_DEVICE_BOOT_FAILED\n",
      .kernel_log = "",
  };
  EXPECT_EQ(CategorizeBootFailure(logs), BootFailureCategory::kVmmFailure);
}

TEST(BootFailureTest, HostSubprocessCrashOnWebRtcAbort) {
  const BootFailureLogs logs{
      .launcher_log =
          "Starting monitored subprocess /tmp/cf/bin/webRTC\n"
          "libc++abi: terminating due to uncaught exception of type "
          "std::__1::system_error: mutex lock failed: Invalid argument\n"
          "run_cvd E 04-20 11:37:56.837 645 705 process_monitor.cc:184] "
          "Detected unexpected exit of monitored subprocess "
          "/tmp/cf/bin/webRTC\n"
          "run_cvd E 04-20 11:37:56.837 645 705 process_monitor.cc:192] "
          "Subprocess /tmp/cf/bin/webRTC (1126) was interrupted by a signal "
          "'Aborted' (6)\n"
          "run_cvd E 04-20 11:37:56.837 645 705 process_monitor.cc:219] "
          "Stopping all monitored processes due to unexpected exit of critical "
          "process\n"
          "VIRTUAL_DEVICE_BOOT_FAILED\n",
  };
  EXPECT_EQ(CategorizeBootFailure(logs),
            BootFailureCategory::kHostSubprocessCrash);
}

TEST(BootFailureTest, NonCriticalMetricsSubprocessExitDoesNotMaskGuestCrash) {
  const BootFailureLogs logs{
      .launcher_log =
          "Starting monitored subprocess /tmp/cf/bin/metrics\n"
          "run_cvd E 04-20 13:15:30.236 635 679 process_monitor.cc:184] "
          "Detected unexpected exit of monitored subprocess "
          "/tmp/cf/bin/metrics\n"
          "GUEST_UBOOT_VERSION: 2021.10-13321-gc0d66ec9a4\n"
          "GUEST_KERNEL_VERSION: 6.18.32-android17\n"
          "VIRTUAL_DEVICE_BOOT_STARTED\n"
          "tombstone_receiver 04-20 13:16:07.882 1037 1037 main.cpp:69] "
          "Creating /tmp/cf/cuttlefish/instances/cvd-1/tombstones/"
          "tombstone_00\n"
          "TimeoutThreadLoop: Did not receive boot completion event in "
          "1000 second(s).\n"
          "Detected unexpected exit of monitored subprocess "
          "/tmp/cf/bin/process_restarter\n",
  };
  EXPECT_EQ(CategorizeBootFailure(logs), BootFailureCategory::kUserspaceCrash);
}

TEST(BootFailureTest, PreBootloaderFailureWhenNoUbootOutput) {
  const BootFailureLogs logs{
      .launcher_log =
          "Starting monitored subprocess /tmp/cf/bin/process_restarter\n"
          "Monitoring subprocesses\n"
          "TimeoutThreadLoop: Did not receive boot completion event in "
          "1000 second(s).\n",
      .kernel_log = "",
  };
  EXPECT_EQ(CategorizeBootFailure(logs),
            BootFailureCategory::kPreBootloaderFailure);
}

TEST(BootFailureTest, BootloaderFailureWhenUbootHangsLoadingRamdisk) {
  const BootFailureLogs logs{
      .launcher_log =
          "Starting monitored subprocess /tmp/cf/bin/process_restarter\n"
          "kernel_log_monitor.cpp:173] GUEST_UBOOT_VERSION: "
          "2021.10-13321-gc0d66ec9a4 (Jul 07 2025)\n",
      .kernel_log =
          "U-Boot 2021.10-13321-gc0d66ec9a4 (Jul 07 2025 - 17:17:45 +0000)\n"
          "ANDROID: Loading vendor ramdisk from \"vendor_boot_a\", "
          "partition 4.\n",
  };
  EXPECT_EQ(CategorizeBootFailure(logs),
            BootFailureCategory::kBootloaderFailure);
}

TEST(BootFailureTest, KernelPanicDetectedInKernelLog) {
  const BootFailureLogs logs{
      .launcher_log =
          "GUEST_UBOOT_VERSION: 2021.10\n"
          "GUEST_KERNEL_VERSION: 6.18.32\n",
      .kernel_log =
          "[    0.000000] Linux version 6.18.32-android17\n"
          "[    1.234567] Kernel panic - not syncing: VFS: Unable to mount "
          "root fs\n",
  };
  EXPECT_EQ(CategorizeBootFailure(logs), BootFailureCategory::kKernelPanic);
}

TEST(BootFailureTest, KernelHangBeforeInitStarts) {
  const BootFailureLogs logs{
      .launcher_log =
          "GUEST_UBOOT_VERSION: 2021.10\n"
          "GUEST_KERNEL_VERSION: 6.18.32\n",
      .kernel_log =
          "[    0.000000] Linux version 6.18.32-android17\n"
          "[    0.500000] PCI: Using configuration type 1 for base access\n",
  };
  EXPECT_EQ(CategorizeBootFailure(logs), BootFailureCategory::kKernelHang);
}

TEST(BootFailureTest, UserspaceCrashDetectedFromKernelUpdatableCrashing) {
  const BootFailureLogs logs{
      .launcher_log =
          "GUEST_UBOOT_VERSION: 2021.10\n"
          "GUEST_KERNEL_VERSION: 6.18.32\n"
          "VIRTUAL_DEVICE_BOOT_STARTED\n",
      .kernel_log =
          "[    0.000000] Linux version 6.18.32-android17\n"
          "[    2.000000] init: init first stage started!\n"
          "[   46.798037] init: process with updatable components 'zygote' "
          "exited 4 times before boot completed\n"
          "[   46.806577] init: processing action "
          "(sys.init.updatable_crashing=1) from "
          "(/system/etc/init/flags_health_check.rc:10)\n",
  };
  EXPECT_EQ(CategorizeBootFailure(logs), BootFailureCategory::kUserspaceCrash);
}

TEST(BootFailureTest, UserspaceCrashDetectedFromLogcatFatalSignal) {
  const BootFailureLogs logs{
      .launcher_log =
          "GUEST_UBOOT_VERSION: 2021.10\n"
          "GUEST_KERNEL_VERSION: 6.18.32\n"
          "VIRTUAL_DEVICE_BOOT_STARTED\n",
      .logcat =
          "04-20 13:57:50.111  1135  1135 F libc    : Fatal signal 4 "
          "(SIGILL), code 2 (ILL_ILLOPN) in tid 1135 (system_server)\n",
  };
  EXPECT_EQ(CategorizeBootFailure(logs), BootFailureCategory::kUserspaceCrash);
}

TEST(BootFailureTest,
     BootTimeoutPreBootStartedWhenInitStartsWithoutBootStarted) {
  const BootFailureLogs logs{
      .launcher_log =
          "GUEST_UBOOT_VERSION: 2021.10\n"
          "GUEST_KERNEL_VERSION: 6.18.32\n"
          "GUEST_BUILD_FINGERPRINT: generic/cf_x86_64_phone/vsoc_x86_64\n",
      .kernel_log =
          "[    0.000000] Linux version 6.18.32-android17\n"
          "[    2.000000] init: init first stage started!\n",
  };
  EXPECT_EQ(CategorizeBootFailure(logs),
            BootFailureCategory::kBootTimeoutPreBootStarted);
}

TEST(BootFailureTest, BootTimeoutPostBootStartedWhenNoCrashDetected) {
  const BootFailureLogs logs{
      .launcher_log =
          "GUEST_UBOOT_VERSION: 2021.10\n"
          "GUEST_KERNEL_VERSION: 6.18.32\n"
          "VIRTUAL_DEVICE_BOOT_STARTED\n"
          "TimeoutThreadLoop: Did not receive boot completion event in "
          "1000 second(s).\n",
      .kernel_log =
          "[    0.000000] Linux version 6.18.32-android17\n"
          "[    2.000000] init: init first stage started!\n",
  };
  EXPECT_EQ(CategorizeBootFailure(logs),
            BootFailureCategory::kBootTimeoutPostBootStarted);
}

}  // namespace
}  // namespace cuttlefish
