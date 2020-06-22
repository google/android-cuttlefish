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

#include "host/libs/vm_manager/vm_manager.h"

#include "common/libs/fs/shared_fd.h"
#include "common/libs/utils/subprocess.h"

namespace vm_manager {

// Starts a guest VM with crosvm. It requires the host package to support the
// qemu-cli capability (for network only).
class CrosvmManager : public VmManager {
 public:
  static const std::string name();
  static bool EnsureInstanceDirExists(const std::string& instance_dir);
  static std::vector<std::string> ConfigureGpu(const std::string& gpu_mode);
  static std::vector<std::string> ConfigureBootDevices();

  CrosvmManager(const cuttlefish::CuttlefishConfig* config);
  virtual ~CrosvmManager() = default;

  std::vector<cuttlefish::Command> StartCommands() override;
};

}  // namespace vm_manager
