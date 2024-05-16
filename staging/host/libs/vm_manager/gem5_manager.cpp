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

#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <unistd.h>

#include <fstream>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <android-base/strings.h>
#include <android-base/logging.h>
#include <vulkan/vulkan.h>

#include "common/libs/utils/result.h"
#include "common/libs/utils/subprocess.h"
#include "host/libs/config/command_source.h"
#include "host/libs/config/cuttlefish_config.h"

using cuttlefish::StringFromEnv;

namespace cuttlefish {
namespace vm_manager {
namespace {

static constexpr char kFsHeader[] = R"CPP_STR_END(import argparse
import devices
import os
import shutil
import m5
from m5.util import addToPath
from m5.objects import *
from m5.options import *
from m5.objects.Ethernet import NSGigE, IGbE_igb, IGbE_e1000, EtherTap
from common import SysPaths
from common import ObjectList
from common import MemConfig
from common.cores.arm import HPI
m5.util.addToPath('../..')
)CPP_STR_END";

static constexpr char kFsMemPci[] = R"CPP_STR_END(
  MemConfig.config_mem(args, root.system)

  pci_devices = []
  pci_devices.append(PciVirtIO(vio=VirtIOConsole(device=Terminal(number=0))))
  pci_devices.append(PciVirtIO(vio=VirtIOConsole(device=Terminal(number=1, outfile="none"))))
  pci_devices.append(PciVirtIO(vio=VirtIOConsole(device=Terminal(number=2))))
  pci_devices.append(PciVirtIO(vio=VirtIOConsole(device=Terminal(number=3, outfile="none"))))
  pci_devices.append(PciVirtIO(vio=VirtIOConsole(device=Terminal(number=4, outfile="none"))))
  pci_devices.append(PciVirtIO(vio=VirtIOConsole(device=Terminal(number=5, outfile="none"))))
  pci_devices.append(PciVirtIO(vio=VirtIOConsole(device=Terminal(number=6, outfile="none"))))
  pci_devices.append(PciVirtIO(vio=VirtIOConsole(device=Terminal(number=7, outfile="none"))))
  pci_devices.append(PciVirtIO(vio=VirtIOConsole(device=Terminal(number=8, outfile="none"))))
  pci_devices.append(PciVirtIO(vio=VirtIOConsole(device=Terminal(number=9, outfile="none"))))

  for each_item in args.disk_image:
    disk_image = CowDiskImage()
    disk_image.child.image_file = SysPaths.disk(each_item)
    pci_devices.append(PciVirtIO(vio=VirtIOBlock(image=disk_image)))

  nic = IGbE_e1000(pci_bus=0, pci_dev=0, pci_func=0, InterruptLine=1, InterruptPin=1)
  pci_devices.append(nic)
  root.system.pci_devices = pci_devices
  for pci_device in root.system.pci_devices:
    root.system.attach_pci(pci_device)

  root.tap = EtherTap(tun_clone_device='/dev/net/tun', tap_device_name='cvd-mtap-01')
  root.tap.tap = nic.interface
  root.system.connect()
)CPP_STR_END";

static constexpr char kFsKernelCmd[] = R"CPP_STR_END(
  kernel_cmd = [
    "lpj=19988480",
    "norandmaps",
    "mem=%s" % args.mem_size,
    "console=hvc0",
    "panic=-1",
    "earlycon=pl011,mmio32,0x1c090000",
    "audit=1",
    "printk.devkmsg=on",
    "firmware_class.path=/vendor/etc/",
    "kfence.sample_interval=500",
    "loop.max_part=7",
    "bootconfig",
    "androidboot.force_normal_boot=1",
  ]
  root.system.workload.command_line = " ".join(kernel_cmd)
  if args.restore is not None:
    m5.instantiate(args.restore)
  else:
    m5.instantiate()

  while True:
    event = m5.simulate()
    msg = event.getCause()
    cur_tick = m5.curTick()
    if msg == "checkpoint":
      backup_path = backup_path = os.path.join(root_dir, "gem5_checkpoint")
      if not os.path.isdir(backup_path):
        os.mkdir(backup_path)

      print("Checkpoint @", cur_tick)
      src_dir = os.path.join(m5.options.outdir, "cpt.%d" % cur_tick)
      backup_path = os.path.join(backup_path, "cpt.%d" % cur_tick)
      m5.checkpoint(src_dir)
      shutil.copytree(src_dir, backup_path)
      print("Checkpoint done.")
    else:
      print("Exit msg: " + msg + " @", cur_tick)
      break
  sys.exit(event.getCode())
)CPP_STR_END";

static constexpr char kFsExeMain[] = R"CPP_STR_END(
if __name__ == "__m5_main__":
  main()
)CPP_STR_END";

void GenerateGem5File(const CuttlefishConfig& config,
                      const CuttlefishConfig::InstanceSpecific& instance) {
  // Gem5 specific config, currently users have to change these config locally (without through launch_cvd input flag) to meet their design
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
  starter_fs_ofstream << kFsHeader << "\n";

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
  starter_fs_ofstream << kFsMemPci;

  // system settings
  starter_fs_ofstream << "  root.system.cpu_cluster = [devices.CpuCluster(root.system, " << num_cores << ", \"" << cpu_freq << "\", \"1.0V\", " << cpu_class << ", " << l1_icache_class << ", " << l1_dcache_class << ", " << walk_cache_class << ", " << l2_Cache_class << ")]\n";
  starter_fs_ofstream << "  root.system.addCaches(has_caches, last_cache_level=2)\n";
  starter_fs_ofstream << "  root.system.realview.setupBootLoader(root.system, SysPaths.binary)\n";
  starter_fs_ofstream << "  root.system.workload.dtb_filename = os.path.join(m5.options.outdir, 'system.dtb')\n";
  starter_fs_ofstream << "  root.system.generateDtb(root.system.workload.dtb_filename)\n";
  starter_fs_ofstream << "  root.system.workload.initrd_filename = \"" << instance.PerInstancePath("initrd.img") << "\"\n";
  starter_fs_ofstream << "  root_dir = \"" << StringFromEnv("HOME", ".") << "\"\n";

  //kernel cmd
  starter_fs_ofstream << kFsKernelCmd << "\n";

  // execute main
  starter_fs_ofstream << kFsExeMain << "\n";
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
  // with properties lead to non-deterministic behavior while loading the
  // HALs.

  std::unordered_map<std::string, std::string> bootconfig_args;

  if (instance.gpu_mode() == kGpuModeGuestSwiftshader) {
    LOG(INFO) << "We are in SwiftShader mode";
    bootconfig_args = {
        {"androidboot.cpuvulkan.version", std::to_string(VK_API_VERSION_1_1)},
        {"androidboot.hardware.gralloc", "minigbm"},
        {"androidboot.hardware.hwcomposer", "ranchu"},
        {"androidboot.hardware.hwcomposer.mode", "noop"},
        {"androidboot.hardware.hwcomposer.display_finder_mode", "gem5"},
        {"androidboot.hardware.egl", "angle"},
        {"androidboot.hardware.vulkan", "pastel"},
        {"androidboot.opengles.version", "196609"},  // OpenGL ES 3.1
    };
  } else if (instance.gpu_mode() == kGpuModeGfxstream) {
    LOG(INFO) << "We are in Gfxstream mode";
    bootconfig_args = {
        {"androidboot.cpuvulkan.version", "0"},
        {"androidboot.hardware.gralloc", "minigbm"},
        {"androidboot.hardware.hwcomposer", "ranchu"},
        {"androidboot.hardware.hwcomposer.display_finder_mode", "gem5"},
        {"androidboot.hardware.egl", "emulation"},
        {"androidboot.hardware.vulkan", "ranchu"},
        {"androidboot.hardware.gltransport", "virtio-gpu-pipe"},
        {"androidboot.opengles.version", "196609"},  // OpenGL ES 3.1
    };
  } else if (instance.gpu_mode() == kGpuModeNone) {
    return {};
  } else {
    return CF_ERR("Unknown GPU mode " << instance.gpu_mode());
  }

  if (!instance.gpu_angle_feature_overrides_enabled().empty()) {
    bootconfig_args["androidboot.hardware.angle_feature_overrides_enabled"] =
        instance.gpu_angle_feature_overrides_enabled();
  }
  if (!instance.gpu_angle_feature_overrides_disabled().empty()) {
    bootconfig_args["androidboot.hardware.angle_feature_overrides_disabled"] =
        instance.gpu_angle_feature_overrides_disabled();
  }

  return bootconfig_args;
}

Result<std::unordered_map<std::string, std::string>>
Gem5Manager::ConfigureBootDevices(
    const CuttlefishConfig::InstanceSpecific& /*instance*/) {
  switch (arch_) {
    case Arch::Arm:
    case Arch::Arm64:
      return {{{"androidboot.boot_devices", "30000000.pci"}}};
    // TODO: Add x86 support
    default:
      return CF_ERR("Unhandled arch");
  }
}

Result<std::vector<MonitorCommand>> Gem5Manager::StartCommands(
    const CuttlefishConfig& config, std::vector<VmmDependencyCommand*>&) {
  auto instance = config.ForDefaultInstance();

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

  Command gem5_cmd(gem5_binary);

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

  gem5_cmd.AddEnvironmentVariable("M5_PATH", config.assembly_dir());

  std::vector<MonitorCommand> commands;
  commands.emplace_back(std::move(gem5_cmd), true);
  return commands;
}

} // namespace vm_manager
} // namespace cuttlefish
