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

#include <fruit/fruit.h>

#include "common/libs/utils/result.h"
#include "common/libs/utils/subprocess.h"
#include "host/libs/config/cuttlefish_config.h"

namespace cuttlefish {
namespace vm_manager {

// Superclass of every guest VM manager.
class VmManager {
 public:
  // This is the number of HVC virtual console ports that should be configured
  // by the VmManager. Because crosvm currently allocates these ports as the
  // first PCI devices, and it does not control the allocation of PCI ID
  // assignments, the number of these ports affects the PCI paths for
  // subsequent PCI devices, and these paths are hard-coded in SEPolicy.
  // Fortunately, HVC virtual console ports can be set up to be "sink" devices,
  // so even if they are disabled and the guest isn't using them, they don't
  // need to consume host resources, except for the PCI ID. Use this trick to
  // keep the number of PCI IDs assigned constant for all flags/vm manager
  // combinations
  static const int kDefaultNumHvcs = 9;

  // This is the number of virtual disks (block devices) that should be
  // configured by the VmManager. Related to the description above regarding
  // HVC ports, this problem can also affect block devices (which are
  // enumerated second) if not all of the block devices are available. Unlike
  // HVC virtual console ports, block devices cannot be configured to be sinks,
  // so we once again leverage HVC virtual console ports to "bump up" the last
  // assigned virtual disk PCI ID (i.e. 2 disks = 7 hvcs, 1 disks = 8 hvcs)
  static constexpr int kMaxDisks = 3;

  // This is the number of virtual disks that contribute to the named partition
  // list (/dev/block/by-name/*) under Android. The partitions names from
  // multiple disks *must not* collide. Normally we have one set of partitions
  // from the powerwashed disk (operating system disk) and another set from
  // the persistent disk
  static const int kDefaultNumBootDevices = 2;

  virtual ~VmManager() = default;

  virtual bool IsSupported() = 0;
  virtual std::vector<std::string> ConfigureGraphics(
      const CuttlefishConfig::InstanceSpecific& instance) = 0;
  virtual std::string ConfigureBootDevices(int num_disks) = 0;

  // Starts the VMM. It will usually build a command and pass it to the
  // command_starter function, although it may start more than one. The
  // command_starter function allows to customize the way vmm commands are
  // started/tracked/etc.
  virtual Result<std::vector<cuttlefish::Command>> StartCommands(
      const CuttlefishConfig& config) = 0;
};

fruit::Component<fruit::Required<const CuttlefishConfig,
                                 const CuttlefishConfig::InstanceSpecific>,
                 VmManager>
VmManagerComponent();

std::unique_ptr<VmManager> GetVmManager(const std::string&, Arch arch);

std::string ConfigureMultipleBootDevices(const std::string& pci_path, int pci_offset,
                                         int num_disks);

} // namespace vm_manager
} // namespace cuttlefish
