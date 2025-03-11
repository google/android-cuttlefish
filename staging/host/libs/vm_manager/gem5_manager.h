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
#pragma once

#include <string>
#include <vector>

#include "host/libs/vm_manager/vm_manager.h"

#include "common/libs/fs/shared_fd.h"

namespace cuttlefish {
namespace vm_manager {

// Starts a guest VM using the gem5 command directly. It requires the host
// package to support the gem5 capability.
class Gem5Manager : public VmManager {
 public:
  static std::string name() { return "gem5"; }

  Gem5Manager(Arch);
  virtual ~Gem5Manager() = default;

  bool IsSupported() override;
  std::vector<std::string> ConfigureGraphics(
      const CuttlefishConfig& config) override;
  std::string ConfigureBootDevices(int num_disks) override;

  std::vector<cuttlefish::Command> StartCommands(
      const CuttlefishConfig& config) override;

 private:
  Arch arch_;
};

const std::string fs_header = R"CPP_STR_END(import argparse
import devices
import os
import m5
from m5.util import addToPath
from m5.objects import *
from m5.options import *
from common import SysPaths
from common import ObjectList
from common import MemConfig
from common.cores.arm import HPI
m5.util.addToPath('../..')
)CPP_STR_END";

const std::string fs_mem_pci = R"CPP_STR_END(
  MemConfig.config_mem(args, root.system)

  pci_devices = []
  pci_devices.append(PciVirtIO(vio=VirtIOConsole(device=Terminal(number=0))))
  pci_devices.append(PciVirtIO(vio=VirtIOConsole(device=Terminal(number=1, outfile="none"))))
  pci_devices.append(PciVirtIO(vio=VirtIOConsole(device=Terminal(number=2))))
  pci_devices.append(PciVirtIO(vio=VirtIOConsole(device=Terminal(number=3, outfile="none"))))
  pci_devices.append(PciVirtIO(vio=VirtIOConsole(device=Terminal(number=4, outfile="none"))))
  pci_devices.append(PciVirtIO(vio=VirtIOConsole(device=Terminal(number=5, outfile="none"))))
  pci_devices.append(PciVirtIO(vio=VirtIOConsole(device=Terminal(number=6, outfile="none"))))

  for each_item in args.disk_image:
    disk_image = CowDiskImage()
    disk_image.child.image_file = SysPaths.disk(each_item)
    pci_devices.append(PciVirtIO(vio=VirtIOBlock(image=disk_image)))

  root.system.pci_devices = pci_devices
  for pci_device in root.system.pci_devices:
    root.system.attach_pci(pci_device)

  root.system.connect()
)CPP_STR_END";

const std::string fs_kernel_cmd = R"CPP_STR_END(
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
  m5.instantiate()
  sys.exit(m5.simulate().getCode())
)CPP_STR_END";

const std::string fs_exe_main = R"CPP_STR_END(
if __name__ == "__m5_main__":
  main()
)CPP_STR_END";

} // namespace vm_manager
} // namespace cuttlefish
