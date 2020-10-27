/*
 * Copyright (C) 2018 The Android Open Source Project
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

#include <common/libs/utils/subprocess.h>
#include <host/libs/config/cuttlefish_config.h>

namespace cuttlefish {
namespace vm_manager {

// Superclass of every guest VM manager.
class VmManager {
 public:
  virtual ~VmManager() = default;

  virtual bool IsSupported() = 0;
  virtual std::vector<std::string> ConfigureGpuMode(const std::string&) = 0;
  virtual std::vector<std::string> ConfigureBootDevices() = 0;

  // Starts the VMM. It will usually build a command and pass it to the
  // command_starter function, although it may start more than one. The
  // command_starter function allows to customize the way vmm commands are
  // started/tracked/etc.
  virtual std::vector<cuttlefish::Command> StartCommands(
      const CuttlefishConfig& config, const std::string& kernel_cmdline) = 0;
};

std::unique_ptr<VmManager> GetVmManager(const std::string&);

} // namespace vm_manager
} // namespace cuttlefish

