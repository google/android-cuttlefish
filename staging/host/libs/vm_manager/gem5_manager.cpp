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

using cuttlefish::StringFromEnv;

namespace cuttlefish {
namespace vm_manager {
namespace {

void LogAndSetEnv(const char* key, const std::string& value) {
  setenv(key, value.c_str(), 1);
  LOG(INFO) << key << "=" << value;
}

void GenerateGem5File(const CuttlefishConfig& config,
                      const CuttlefishConfig::InstanceSpecific& instance) {
  // Gem5 specific config, currently users have to change these config locally (without throug launch_cvd input flag) to meet their design
  // TODO: Add these config into launch_cvd input flag or parse from one json file
  std::string cpu_class = "AtomicSimpleCPU";
  std::string l1_icache_class = "None";
  std::string l1_dcache_class = "None";
  std::string walk_cache_class = "None";
  std::string l2_Cache_class = "None";
  std::string cpu_freq = "4GHz";
  int num_cores = 1;
  std::string mem_type = "DDR3_1600_8x8";
  int mem_channels = 1;
  std::string mem_ranks = "None";

  // start generating starter_fs.py
  std::string fs_path = instance.gem5_binary_dir() +
                        "/configs/example/arm/starter_fs.py";
  std::ofstream starter_fs_ofstream(fs_path.c_str());
  starter_fs_ofstream << fs_header << "\n";

  // global vars in python
  starter_fs_ofstream << "default_disk = 'linaro-minimal-aarch64.img'\n";

  // main function
  starter_fs_ofstream << "def main():\n";

  // args
  starter_fs_ofstream << "  parser = argparse.ArgumentParser(epilog=__doc__)\n";
  starter_fs_ofstream << "  parser.add_argument(\"--disk-image\", action=\"append\", type=str, default=[])\n";
  starter_fs_ofstream << "  parser.add_argument(\"--mem-type\", default=\"" << mem_type << "\", choices=ObjectList.mem_list.get_names())\n";
  starter_fs_ofstream << "  parser.add_argument(\"--mem-channels\", type=int, default=" << mem_channels << ")\n";
  starter_fs_ofstream << "  parser.add_argument(\"--mem-ranks\", type=int, default=" << mem_ranks << ")\n";
  starter_fs_ofstream << "  parser.add_argument(\"--mem-size\", action=\"store\", type=str, default=\"" << instance.memory_mb() << "MB\")\n";
  starter_fs_ofstream << "  parser.add_argument(\"--restore\", type=str, default=None)\n";
  starter_fs_ofstream << "  args = parser.parse_args()\n";

  // instantiate system
  starter_fs_ofstream << "  root = Root(full_system=True)\n";
  starter_fs_ofstream << "  mem_mode = " << cpu_class << ".memory_mode()\n";
  starter_fs_ofstream << "  has_caches = True if mem_mode == \"timing\" else False\n";
  starter_fs_ofstream << "  root.system = devices.SimpleSystem(has_caches, args.mem_size, mem_mode=mem_mode, workload=ArmFsLinux(object_file=SysPaths.binary(\"" << config.assembly_dir() << "/kernel\")))\n";

  // mem config and pci instantiate
  starter_fs_ofstream << fs_mem_pci;

  // system settings
  starter_fs_ofstream << "  root.system.cpu_cluster = [devices.CpuCluster(root.system, " << num_cores << ", \"" << cpu_freq << "\", \"1.0V\", " << cpu_class << ", " << l1_icache_class << ", " << l1_dcache_class << ", " << walk_cache_class << ", " << l2_Cache_class << ")]\n";
  starter_fs_ofstream << "  root.system.addCaches(has_caches, last_cache_level=2)\n";
  starter_fs_ofstream << "  root.system.realview.setupBootLoader(root.system, SysPaths.binary)\n";
  starter_fs_ofstream << "  root.system.workload.dtb_filename = os.path.join(m5.options.outdir, 'system.dtb')\n";
  starter_fs_ofstream << "  root.system.generateDtb(root.system.workload.dtb_filename)\n";
  starter_fs_ofstream << "  root.system.workload.initrd_filename = \"" << instance.PerInstancePath("initrd.img") << "\"\n";
  starter_fs_ofstream << "  root_dir = \"" << StringFromEnv("HOME", ".") << "\"\n";

  //kernel cmd
  starter_fs_ofstream << fs_kernel_cmd << "\n";

  // execute main
  starter_fs_ofstream << fs_exe_main << "\n";
}

}  // namespace

Gem5Manager::Gem5Manager(Arch arch) : arch_(arch) {}

bool Gem5Manager::IsSupported() {
  return HostSupportsQemuCli();
}

Result<std::unordered_map<std::string, std::string>>
Gem5Manager::ConfigureGraphics(
    const CuttlefishConfig::InstanceSpecific& instance) {
  // TODO: Add support for the gem5 gpu models

  // Override the default HAL search paths in all cases. We do this because
  // the HAL search path allows for fallbacks, and fallbacks in conjunction
  // with properities lead to non-deterministic behavior while loading the
  // HALs.
  return {{
      {"androidboot.cpuvulkan.version", std::to_string(VK_API_VERSION_1_1)},
      {"androidboot.hardware.gralloc", "minigbm"},
      {"androidboot.hardware.hwcomposer", instance.hwcomposer()},
      {"androidboot.hardware.hwcomposer.mode", "noop"},
      {"androidboot.hardware.egl", "angle"},
      {"androidboot.hardware.vulkan", "pastel"},
  }};
}

Result<std::unordered_map<std::string, std::string>>
Gem5Manager::ConfigureBootDevices(int /*num_disks*/, bool /*have_gpu*/) {
  switch (arch_) {
    case Arch::Arm:
    case Arch::Arm64:
      return {{{"androidboot.boot_devices", "30000000.pci"}}};
    // TODO: Add x86 support
    default:
      return CF_ERR("Unhandled arch");
  }
}

Result<std::vector<Command>> Gem5Manager::StartCommands(
    const CuttlefishConfig& config) {
  auto instance = config.ForDefaultInstance();

  auto stop = [](Subprocess* proc) {
    return KillSubprocess(proc) == StopperResult::kStopSuccess
               ? StopperResult::kStopCrash
               : StopperResult::kStopFailure;
  };
  std::string gem5_binary = instance.gem5_binary_dir();
  switch (arch_) {
    case Arch::Arm:
    case Arch::Arm64:
      gem5_binary += "/build/ARM/gem5.opt";
      break;
    case Arch::RiscV64:
      gem5_binary += "/build/RISCV/gem5.opt";
      break;
    case Arch::X86:
    case Arch::X86_64:
      gem5_binary += "/build/X86/gem5.opt";
      break;
  }
  // generate Gem5 starter_fs.py before we execute it
  GenerateGem5File(config, instance);

  Command gem5_cmd(gem5_binary, stop);

  // Always enable listeners, because auto mode will disable once it detects
  // gem5 is not run interactively
  gem5_cmd.AddParameter("--listener-mode=on");

  // Add debug-flags and debug-file before the script (i.e. starter_fs.py).
  // We check the flags are not empty first since they are optional
  if(!config.gem5_debug_flags().empty()) {
    gem5_cmd.AddParameter("--debug-flags=", config.gem5_debug_flags());
    if(!instance.gem5_debug_file().empty()) {
      gem5_cmd.AddParameter("--debug-file=", instance.gem5_debug_file());
    }
  }

  gem5_cmd.AddParameter(instance.gem5_binary_dir(),
                        "/configs/example/arm/starter_fs.py");

  // restore checkpoint case
  if (instance.gem5_checkpoint_dir() != "") {
    gem5_cmd.AddParameter("--restore=",
                          instance.gem5_checkpoint_dir());
  }

  gem5_cmd.AddParameter("--mem-size=", instance.memory_mb() * 1024ULL * 1024ULL);
  for (const auto& disk : instance.virtual_disk_paths()) {
    gem5_cmd.AddParameter("--disk-image=", disk);
  }

  LogAndSetEnv("M5_PATH", config.assembly_dir());

  std::vector<Command> ret;
  ret.push_back(std::move(gem5_cmd));
  return ret;
}

} // namespace vm_manager
} // namespace cuttlefish
