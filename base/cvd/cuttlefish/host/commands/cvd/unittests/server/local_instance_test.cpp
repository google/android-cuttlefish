//
// Copyright (C) 2023 The Android Open Source Project
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

#include <random>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "common/libs/utils/contains.h"
#include "host/commands/cvd/types.h"
#include "host/commands/cvd/unittests/server/cmd_runner.h"
#include "host/commands/cvd/unittests/server/local_instance_helper.h"

namespace cuttlefish {
namespace acloud {

TEST(CvdDriver, CvdLocalInstance) {
  cvd_common::Envs envs;
  CmdRunner::Run("cvd reset", envs);

  // 1st test normal case
  auto cmd_local_instance_local_image =
      CmdRunner::Run("cvd acloud create --local-instance --local-image", envs);
  ASSERT_TRUE(cmd_local_instance_local_image.Success())
      << cmd_local_instance_local_image.Stderr();
  auto cmd_stop = CmdRunner::Run("cvd stop", envs);
  ASSERT_TRUE(cmd_stop.Success()) << cmd_stop.Stderr();

  // 2nd test random id input
  std::random_device rd;
  std::default_random_engine mt(rd());
  std::uniform_int_distribution<int> dist(1, 10);

  // randomly generate instance id within 1-10, id 0 has been used
  std::string id = std::to_string(dist(mt));
  std::string cmd_str = "cvd acloud create --local-instance " + id;
  cmd_str += " --local-image";
  auto cmd_id = CmdRunner::Run(cmd_str, envs);
  ASSERT_TRUE(cmd_id.Success()) << cmd_id.Stderr();

  auto cmd_fleet = CmdRunner::Run("cvd fleet", envs);
  ASSERT_TRUE(cmd_fleet.Success()) << cmd_fleet.Stderr();
  ASSERT_TRUE(Contains(cmd_fleet.Stdout(), "cvd-" + id));

  cmd_stop = CmdRunner::Run("cvd stop", envs);
  ASSERT_TRUE(cmd_stop.Success()) << cmd_stop.Stderr();

  cmd_fleet = CmdRunner::Run("cvd fleet", envs);
  ASSERT_TRUE(cmd_fleet.Success()) << cmd_fleet.Stderr();
  ASSERT_FALSE(Contains(cmd_fleet.Stdout(), "cvd-" + id));

  // 3rd test local instance --local-boot-image
  const auto product_out_dir = StringFromEnv("ANDROID_PRODUCT_OUT", "");
  cmd_str =
      "cvd acloud create --local-instance --local-image --local-boot-image " +
      product_out_dir;
  cmd_str += "/boot.img";
  auto cmd_local_boot_image = CmdRunner::Run(cmd_str, envs);
  ASSERT_TRUE(cmd_local_boot_image.Success()) << cmd_local_boot_image.Stderr();
  cmd_stop = CmdRunner::Run("cvd stop", envs);
  ASSERT_TRUE(cmd_stop.Success()) << cmd_stop.Stderr();

  // clean up for the next test
  CmdRunner::Run("cvd reset", envs);
}

TEST_F(CvdInstanceLocalTest, CvdLocalInstanceRemoteImage) {
  // 4th test local instance, remote image, --branch, --build-id flags
  auto cmd_result = Execute("cvd acloud create --local-instance --build-id "
      "9759836 --branch git_master --build-target cf_x86_64_phone-userdebug "
      "--bootloader-branch aosp_u-boot-mainline --bootloader-build-id "
      "9602025 --bootloader-build-target u-boot_crosvm_x86_64");
  ASSERT_TRUE(cmd_result.Success()) << cmd_result.Stderr();
}

TEST(CvdDriver, CvdLocalInstanceRemoteImageKernelImage) {
  cvd_common::Envs envs;
  CmdRunner::Run("cvd reset", envs);

  // 5th test local instance, remote image, --kernel-branch, --kernel-build-id,
  // --kernel-build-target, --image-download-dir --build-target flags
  auto cmd_kernel_build = CmdRunner::Run(
      "cvd acloud create --local-instance --branch "
      "git_master --build-target cf_x86_64_phone-userdebug --kernel-branch "
      "aosp_kernel-common-android13-5.10 --kernel-build-id 9600402 "
      "--kernel-build-target kernel_virt_x86_64 --image-download-dir "
      "/tmp/acloud_cvd_temp/test123",
      envs);
  ASSERT_TRUE(cmd_kernel_build.Success()) << cmd_kernel_build.Stderr();
  auto cmd_stop = CmdRunner::Run("cvd stop", envs);
  // after this command, the 5.10 kernel image should be downloaded at
  // /tmp/acloud_cvd_temp/test123/acloud_image_artifacts/9594220cf_x86_64_phone-userdebug
  // I will re-use this pre-built kernel image for later testing

  // 6th test local instance, local-kernel-image, --branch
  auto cmd_local_kernel_image = CmdRunner::Run(
      "cvd acloud create --local-instance --branch git_master  --build-target "
      "cf_x86_64_phone-userdebug --local-kernel-image "
      "/tmp/acloud_cvd_temp/test123/acloud_image_artifacts/"
      "9695745cf_x86_64_phone-userdebug",
      envs);
  ASSERT_TRUE(cmd_local_kernel_image.Success())
      << cmd_local_kernel_image.Stderr();
  cmd_stop = CmdRunner::Run("cvd stop", envs);

  // clean up for the next test
  CmdRunner::Run("cvd reset", envs);
}

// CvdInstanceLocalTest is testing different flags with "cvd acloud create --local-instance"
TEST_F(CvdInstanceLocalTest, CvdLocalInstanceRemoteImageBootloader) {
  // 7th test --bootloader-branch --bootloader-build-id
  // --bootloader-build-target
  auto cmd_result = Execute("cvd acloud create --local-instance "
      "--branch git_master --build-target cf_x86_64_phone-userdebug "
      "--bootloader-branch aosp_u-boot-mainline --bootloader-build-id 9602025 "
      "--bootloader-build-target u-boot_crosvm_x86_64");
  ASSERT_TRUE(cmd_result.Success()) << cmd_result.Stderr();
}

TEST_F(CvdInstanceLocalTest, CvdLocalInstanceRemoteImageSystem) {
  // 8th --system-branch, --system-build-id, --system-build-target
  auto cmd_result = Execute("cvd acloud create --local-instance --branch git_master "
      "--build-target cf_x86_64_phone-userdebug --system-branch git_master "
      "--system-build-id 9684420 --system-build-target aosp_x86_64-userdebug");
  ASSERT_TRUE(cmd_result.Success()) << cmd_result.Stderr();
}

TEST_F(CvdInstanceLocalTest, Empty) {
  if (!SetUpOk()) {
    GTEST_SKIP() << Error().msg;
  }
}

}  // namespace acloud
}  // namespace cuttlefish
