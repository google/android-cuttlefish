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

#include "host/libs/vm_manager/gem5_manager.h"

#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include <android-base/strings.h>
#include <android-base/logging.h>
#include <vulkan/vulkan.h>

#include "common/libs/fs/shared_select.h"
#include "common/libs/utils/files.h"
#include "common/libs/utils/subprocess.h"
#include "common/libs/utils/users.h"
#include "host/libs/config/cuttlefish_config.h"
#include "host/libs/config/known_paths.h"

namespace cuttlefish {
namespace vm_manager {
namespace {

void LogAndSetEnv(const char* key, const std::string& value) {
  setenv(key, value.c_str(), 1);
  LOG(INFO) << key << "=" << value;
}

}  // namespace

Gem5Manager::Gem5Manager(Arch arch) : arch_(arch) {}

bool Gem5Manager::IsSupported() {
  return HostSupportsQemuCli();
}

std::vector<std::string> Gem5Manager::ConfigureGraphics(
    const CuttlefishConfig& config) {
  // TODO: Add support for the gem5 gpu models

  // Override the default HAL search paths in all cases. We do this because
  // the HAL search path allows for fallbacks, and fallbacks in conjunction
  // with properities lead to non-deterministic behavior while loading the
  // HALs.
  return {
      "androidboot.cpuvulkan.version=" + std::to_string(VK_API_VERSION_1_1),
      "androidboot.hardware.gralloc=minigbm",
      "androidboot.hardware.hwcomposer=" + config.hwcomposer(),
      "androidboot.hardware.egl=angle",
      "androidboot.hardware.vulkan=pastel",
  };
}

std::string Gem5Manager::ConfigureBootDevices(int /*num_disks*/) {
  switch (arch_) {
    case Arch::Arm:
    case Arch::Arm64:
      return "androidboot.boot_devices=30000000.pci";
    // TODO: Add x86 support
    default:
      return "";
  }
}

std::vector<Command> Gem5Manager::StartCommands(
    const CuttlefishConfig& config) {
  auto instance = config.ForDefaultInstance();

  auto stop = [](Subprocess* proc) {
    return KillSubprocess(proc) == StopperResult::kStopSuccess
               ? StopperResult::kStopCrash
               : StopperResult::kStopFailure;
  };
  std::string gem5_binary = config.gem5_binary_dir();
  switch (arch_) {
    case Arch::Arm:
    case Arch::Arm64:
      gem5_binary += "/build/ARM/gem5.opt";
      break;
    case Arch::X86:
    case Arch::X86_64:
      gem5_binary += "/build/X86/gem5.opt";
      break;
  }
  Command gem5_cmd(gem5_binary, stop);

  gem5_cmd.AddParameter(config.gem5_binary_dir(), "/configs/example/arm/starter_fs.py");

  gem5_cmd.AddParameter("--mem-size=", config.memory_mb() * 1024ULL * 1024ULL);

  gem5_cmd.AddParameter("--cpu=atomic");
  gem5_cmd.AddParameter("--num-cores=", config.cpus());

  for (const auto& disk : instance.virtual_disk_paths()) {
    gem5_cmd.AddParameter("--disk-image=", disk);
  }

  gem5_cmd.AddParameter("--kernel=", config.assembly_dir(), "/kernel");
  gem5_cmd.AddParameter("--initrd=", instance.PerInstancePath("initrd.img"));

  LogAndSetEnv("M5_PATH", config.assembly_dir());

  std::vector<Command> ret;
  ret.push_back(std::move(gem5_cmd));
  return ret;
}

} // namespace vm_manager
} // namespace cuttlefish
