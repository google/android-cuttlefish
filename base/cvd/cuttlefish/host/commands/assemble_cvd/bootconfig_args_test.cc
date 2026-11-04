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

#include "cuttlefish/host/commands/assemble_cvd/bootconfig_args.h"

#include <set>
#include <string>
#include <unordered_map>

#include "gmock/gmock-matchers.h"
#include "gtest/gtest.h"

#include "cuttlefish/common/libs/utils/host_info.h"
#include "cuttlefish/host/libs/config/cuttlefish_config.h"
#include "cuttlefish/host/libs/config/gpu_mode.h"
#include "cuttlefish/host/libs/config/secure_hals.h"
#include "cuttlefish/host/libs/config/vmm_mode.h"
#include "cuttlefish/result/assert.h"
#include "cuttlefish/result/result_matchers.h"

namespace cuttlefish {
namespace {

using ::testing::Contains;
using ::testing::Pair;

TEST(BootconfigArgsTest, GuestKeymintAndGatekeeperNoneDisableCuttlefishApexes) {
  CuttlefishConfig config;
  config.set_vm_manager(VmmMode::kCrosvm);
  config.set_secure_hals({
      SecureHal::kHostOemlockSecure,
      SecureHal::kGuestKeymintNone,
      SecureHal::kGuestGatekeeperNone,
  });
  CuttlefishConfig::MutableInstanceSpecific mutable_instance =
      config.ForInstance(1);
  mutable_instance.set_target_arch(Arch::X86_64);
  mutable_instance.set_gpu_mode(GpuMode::None);
  mutable_instance.set_virtual_disk_paths({"disk.img"});

  const CuttlefishConfig& const_config = config;
  const CuttlefishConfig::InstanceSpecific instance =
      const_config.ForInstance(1);
  const std::unordered_map<std::string, std::string> args =
      CF_ASSERT(BootconfigArgsFromConfig(const_config, instance, {}));

  EXPECT_THAT(args, Contains(Pair(
                        "androidboot.vendor.apex.com.android.hardware.keymint",
                        "none")));
  EXPECT_THAT(
      args,
      Contains(Pair("androidboot.vendor.apex.com.android.hardware.gatekeeper",
                    "none")));
  EXPECT_THAT(
      args, Contains(Pair("androidboot.vendor.apex.com.android.hardware.weaver",
                          "none")));
}

TEST(BootconfigArgsTest, GuestKeymintNoneOnlyDisablesKeymintApex) {
  CuttlefishConfig config;
  config.set_vm_manager(VmmMode::kCrosvm);
  config.set_secure_hals({SecureHal::kGuestKeymintNone});
  CuttlefishConfig::MutableInstanceSpecific mutable_instance =
      config.ForInstance(1);
  mutable_instance.set_target_arch(Arch::X86_64);
  mutable_instance.set_gpu_mode(GpuMode::None);
  mutable_instance.set_virtual_disk_paths({"disk.img"});

  const CuttlefishConfig& const_config = config;
  const CuttlefishConfig::InstanceSpecific instance =
      const_config.ForInstance(1);
  const std::unordered_map<std::string, std::string> args =
      CF_ASSERT(BootconfigArgsFromConfig(const_config, instance, {}));

  EXPECT_THAT(args, Contains(Pair(
                        "androidboot.vendor.apex.com.android.hardware.keymint",
                        "none")));
  EXPECT_THAT(
      args,
      Contains(Pair("androidboot.vendor.apex.com.android.hardware.gatekeeper",
                    "com.android.hardware.gatekeeper.cf_remote")));
}

TEST(BootconfigArgsTest, GuestGatekeeperNoneOnlyDisablesGatekeeperApex) {
  CuttlefishConfig config;
  config.set_vm_manager(VmmMode::kCrosvm);
  config.set_secure_hals({SecureHal::kGuestGatekeeperNone});
  CuttlefishConfig::MutableInstanceSpecific mutable_instance =
      config.ForInstance(1);
  mutable_instance.set_target_arch(Arch::X86_64);
  mutable_instance.set_gpu_mode(GpuMode::None);
  mutable_instance.set_virtual_disk_paths({"disk.img"});

  const CuttlefishConfig& const_config = config;
  const CuttlefishConfig::InstanceSpecific instance =
      const_config.ForInstance(1);
  const std::unordered_map<std::string, std::string> args =
      CF_ASSERT(BootconfigArgsFromConfig(const_config, instance, {}));

  EXPECT_THAT(args, Contains(Pair(
                        "androidboot.vendor.apex.com.android.hardware.keymint",
                        "com.android.hardware.keymint.rust_cf_remote")));
  EXPECT_THAT(
      args,
      Contains(Pair("androidboot.vendor.apex.com.android.hardware.gatekeeper",
                    "none")));
}

TEST(BootconfigArgsTest, OemlockOnlyWithTi50EmulatorFallsBackToRemoteApexes) {
  CuttlefishConfig config;
  config.set_vm_manager(VmmMode::kCrosvm);
  config.set_secure_hals({SecureHal::kHostOemlockSecure});
  CuttlefishConfig::MutableInstanceSpecific mutable_instance =
      config.ForInstance(1);
  mutable_instance.set_target_arch(Arch::X86_64);
  mutable_instance.set_gpu_mode(GpuMode::None);
  mutable_instance.set_virtual_disk_paths({"disk.img"});
  mutable_instance.set_ti50_emulator("/path/to/ti50a-emulator");

  const CuttlefishConfig& const_config = config;
  const CuttlefishConfig::InstanceSpecific instance =
      const_config.ForInstance(1);
  const std::unordered_map<std::string, std::string> args =
      CF_ASSERT(BootconfigArgsFromConfig(const_config, instance, {}));

  EXPECT_THAT(args, Contains(Pair(
                        "androidboot.vendor.apex.com.android.hardware.keymint",
                        "com.android.hardware.keymint.rust_cf_remote")));
  EXPECT_THAT(
      args,
      Contains(Pair("androidboot.vendor.apex.com.android.hardware.gatekeeper",
                    "com.android.hardware.gatekeeper.cf_remote")));
  EXPECT_THAT(
      args, Contains(Pair("androidboot.vendor.apex.com.android.hardware.weaver",
                          "none")));
}

TEST(BootconfigArgsTest, DefaultSecureHalsSelectsCuttlefishApexes) {
  CuttlefishConfig config;
  config.set_vm_manager(VmmMode::kCrosvm);
  config.set_secure_hals({
      SecureHal::kHostOemlockSecure,
      SecureHal::kGuestKeymintInsecure,
      SecureHal::kGuestGatekeeperInsecure,
      SecureHal::kHostWeaverSecure,
  });
  CuttlefishConfig::MutableInstanceSpecific mutable_instance =
      config.ForInstance(1);
  mutable_instance.set_target_arch(Arch::X86_64);
  mutable_instance.set_gpu_mode(GpuMode::None);
  mutable_instance.set_virtual_disk_paths({"disk.img"});

  const CuttlefishConfig& const_config = config;
  const CuttlefishConfig::InstanceSpecific instance =
      const_config.ForInstance(1);
  const std::unordered_map<std::string, std::string> args =
      CF_ASSERT(BootconfigArgsFromConfig(const_config, instance, {}));

  EXPECT_THAT(args, Contains(Pair(
                        "androidboot.vendor.apex.com.android.hardware.keymint",
                        "com.android.hardware.keymint.rust_nonsecure")));
  EXPECT_THAT(
      args,
      Contains(Pair("androidboot.vendor.apex.com.android.hardware.gatekeeper",
                    "com.android.hardware.gatekeeper.nonsecure")));
  EXPECT_THAT(
      args, Contains(Pair("androidboot.vendor.apex.com.android.hardware.weaver",
                          "com.android.hardware.weaver.cf")));
}

TEST(BootconfigArgsTest, HostSecureHalsSelectsRemoteCuttlefishApexes) {
  CuttlefishConfig config;
  config.set_vm_manager(VmmMode::kCrosvm);
  config.set_secure_hals({
      SecureHal::kHostKeymintSecure,
      SecureHal::kHostGatekeeperSecure,
  });
  CuttlefishConfig::MutableInstanceSpecific mutable_instance =
      config.ForInstance(1);
  mutable_instance.set_target_arch(Arch::X86_64);
  mutable_instance.set_gpu_mode(GpuMode::None);
  mutable_instance.set_virtual_disk_paths({"disk.img"});

  const CuttlefishConfig& const_config = config;
  const CuttlefishConfig::InstanceSpecific instance =
      const_config.ForInstance(1);
  const std::unordered_map<std::string, std::string> args =
      CF_ASSERT(BootconfigArgsFromConfig(const_config, instance, {}));

  EXPECT_THAT(args, Contains(Pair(
                        "androidboot.vendor.apex.com.android.hardware.keymint",
                        "com.android.hardware.keymint.rust_cf_remote")));
  EXPECT_THAT(
      args,
      Contains(Pair("androidboot.vendor.apex.com.android.hardware.gatekeeper",
                    "com.android.hardware.gatekeeper.cf_remote")));
}

TEST(BootconfigArgsTest, ParseFormatAndValidateGuestNoneSecureHals) {
  const std::set<SecureHal> hals = CF_ASSERT(
      ParseSecureHals("oemlock,guest_keymint_none,guest_gatekeeper_none"));
  const std::set<SecureHal> expected{
      SecureHal::kHostOemlockSecure,
      SecureHal::kGuestKeymintNone,
      SecureHal::kGuestGatekeeperNone,
  };
  EXPECT_EQ(hals, expected);
  EXPECT_EQ(ToString(SecureHal::kGuestKeymintNone), "guest_keymint_none");
  EXPECT_EQ(ToString(SecureHal::kGuestGatekeeperNone), "guest_gatekeeper_none");

  EXPECT_THAT(ValidateSecureHals(hals), IsOk());
  EXPECT_THAT(ValidateSecureHals({SecureHal::kGuestKeymintNone,
                                  SecureHal::kGuestGatekeeperInsecure}),
              IsOk());
  EXPECT_THAT(ValidateSecureHals({SecureHal::kGuestGatekeeperNone,
                                  SecureHal::kGuestKeymintInsecure}),
              IsOk());

  EXPECT_THAT(ValidateSecureHals({SecureHal::kGuestKeymintNone,
                                  SecureHal::kGuestKeymintInsecure}),
              IsError());
  EXPECT_THAT(ValidateSecureHals({SecureHal::kGuestKeymintNone,
                                  SecureHal::kHostKeymintSecure}),
              IsError());
  EXPECT_THAT(ValidateSecureHals({SecureHal::kGuestGatekeeperNone,
                                  SecureHal::kGuestGatekeeperInsecure}),
              IsError());
  EXPECT_THAT(ValidateSecureHals({SecureHal::kGuestGatekeeperNone,
                                  SecureHal::kHostGatekeeperSecure}),
              IsError());
}

}  // namespace
}  // namespace cuttlefish
