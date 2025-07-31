/*
 * Copyright (C) 2019 The Android Open Source Project
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
#include "cuttlefish/host/commands/assemble_cvd/flags/vm_manager.h"

#include <string>
#include <string_view>
#include <vector>

#include <android-base/strings.h>
#include <gflags/gflags.h>

#include "cuttlefish/common/libs/utils/result.h"
#include "cuttlefish/host/commands/assemble_cvd/flags_defaults.h"
#include "cuttlefish/host/commands/assemble_cvd/guest_config.h"
#include "cuttlefish/host/libs/config/vmm_mode.h"

DEFINE_string(vm_manager, CF_DEFAULTS_VM_MANAGER,
              "What virtual machine manager to use, one of {qemu_cli, crosvm}");

namespace cuttlefish {

Result<VmManagerFlag> VmManagerFlag::FromGlobalGflags(
    const std::vector<GuestConfig>& guest_configs) {
  // TODO: b/250988697 - Support multiple multiple VM managers in one group
  CF_EXPECT(!guest_configs.empty());
  for (const GuestConfig& guest_config : guest_configs) {
    CF_EXPECT_EQ(guest_config.target_arch, guest_configs[0].target_arch,
                 "All instance target architectures should be the same");
  }

  std::vector<std::string> vm_manager_str_vec =
      android::base::Split(FLAGS_vm_manager, ",");

  VmmMode default_vmm = IsHostCompatible(guest_configs[0].target_arch)
                            ? VmmMode::kCrosvm
                            : VmmMode::kQemu;

  std::vector<VmmMode> vmm_vec;
  for (std::string_view vmm_str : vm_manager_str_vec) {
    vmm_vec.emplace_back(vmm_str.empty() ? default_vmm
                                         : CF_EXPECT(ParseVmm(vmm_str)));
  }
  if (vmm_vec.empty()) {
    vmm_vec.emplace_back(default_vmm);
  }
  for (VmmMode mode : vmm_vec) {
    CF_EXPECT_EQ(mode, vmm_vec[0], "All VMMs must be the same");
  }
  return VmManagerFlag(vmm_vec[0]);
}

VmManagerFlag::VmManagerFlag(VmmMode mode) : mode_(mode) {}

VmmMode VmManagerFlag::Mode() const { return mode_; }

}  // namespace cuttlefish
