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
#pragma once

#include <string>
#include <vector>

#include "common/libs/fs/shared_fd.h"
#include "common/libs/utils/result.h"
#include "common/libs/utils/subprocess.h"
#include "host/libs/vm_manager/vm_manager.h"

namespace cuttlefish {
namespace vm_manager {

// Starts a guest VM with crosvm. It requires the host package to support the
// qemu-cli capability (for network only).
class CrosvmManager : public VmManager {
 public:
  static std::string name() { return "crosvm"; }
  virtual ~CrosvmManager() = default;

  bool IsSupported() override;
  std::vector<std::string> ConfigureGraphics(
      const CuttlefishConfig::InstanceSpecific& instance) override;
  std::string ConfigureBootDevices(int num_disks, bool have_gpu) override;

  Result<std::vector<cuttlefish::Command>> StartCommands(
      const CuttlefishConfig& config) override;

 private:
  static constexpr int kCrosvmVmResetExitCode = 32;
};

} // namespace vm_manager
} // namespace cuttlefish
