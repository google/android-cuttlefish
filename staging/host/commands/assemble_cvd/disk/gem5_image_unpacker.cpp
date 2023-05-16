/*
 * Copyright (C) 2023 The Android Open Source Project
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

#include "host/commands/assemble_cvd/disk/disk.h"

#include <fruit/fruit.h>

#include "common/libs/utils/files.h"
#include "host/commands/assemble_cvd/boot_image_utils.h"
#include "host/libs/vm_manager/gem5_manager.h"

namespace cuttlefish {

class Gem5ImageUnpacker : public SetupFeature {
 public:
  INJECT(Gem5ImageUnpacker(const CuttlefishConfig& config,
                           KernelRamdiskRepacker& bir))
      : config_(config), bir_(bir) {}

  // SetupFeature
  std::string Name() const override { return "Gem5ImageUnpacker"; }

  std::unordered_set<SetupFeature*> Dependencies() const override {
    return {
        static_cast<SetupFeature*>(&bir_),
    };
  }

  bool Enabled() const override {
    // Everything has a bootloader except gem5, so only run this for gem5
    return config_.vm_manager() == vm_manager::Gem5Manager::name();
  }

 protected:
  Result<void> ResultSetup() override {
    const CuttlefishConfig::InstanceSpecific& instance_ =
        config_.ForDefaultInstance();

    /* Unpack the original or repacked boot and vendor boot ramdisks, so that
     * we have access to the baked bootconfig and raw compressed ramdisks.
     * This allows us to emulate what a bootloader would normally do, which
     * Gem5 can't support itself. This code also copies the kernel again
     * (because Gem5 only supports raw vmlinux) and handles the bootloader
     * binaries specially. This code is just part of the solution; it only
     * does the parts which are instance agnostic.
     */

    CF_EXPECT(FileHasContent(instance_.boot_image()), instance_.boot_image());

    const std::string unpack_dir = config_.assembly_dir();
    // The init_boot partition is be optional for testing boot.img
    // with the ramdisk inside.
    if (!FileHasContent(instance_.init_boot_image())) {
      LOG(WARNING) << "File not found: " << instance_.init_boot_image();
    } else {
      CF_EXPECT(UnpackBootImage(instance_.init_boot_image(), unpack_dir),
                "Failed to extract the init boot image");
    }

    CF_EXPECT(FileHasContent(instance_.vendor_boot_image()),
              instance_.vendor_boot_image());

    CF_EXPECT(UnpackVendorBootImageIfNotUnpacked(instance_.vendor_boot_image(),
                                                 unpack_dir),
              "Failed to extract the vendor boot image");

    // Assume the user specified a kernel manually which is a vmlinux
    CF_EXPECT(
        cuttlefish::Copy(instance_.kernel_path(), unpack_dir + "/kernel"));

    // Gem5 needs the bootloader binary to be a specific directory structure
    // to find it. Create a 'binaries' directory and copy it into there
    const std::string binaries_dir = unpack_dir + "/binaries";
    CF_EXPECT(mkdir(binaries_dir.c_str(),
                    S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH) == 0 ||
                  errno == EEXIST,
              "\"" << binaries_dir << "\": " << strerror(errno));
    CF_EXPECT(cuttlefish::Copy(
        instance_.bootloader(),
        binaries_dir + "/" + cpp_basename(instance_.bootloader())));

    // Gem5 also needs the ARM version of the bootloader, even though it
    // doesn't use it. It'll even open it to check it's a valid ELF file.
    // Work around this by copying such a named file from the same directory
    CF_EXPECT(
        cuttlefish::Copy(cpp_dirname(instance_.bootloader()) + "/boot.arm",
                         binaries_dir + "/boot.arm"));

    return {};
  }

 private:
  const CuttlefishConfig& config_;
  KernelRamdiskRepacker& bir_;
};

fruit::Component<fruit::Required<const CuttlefishConfig, KernelRamdiskRepacker>>
Gem5ImageUnpackerComponent() {
  return fruit::createComponent()
      .addMultibinding<SetupFeature, Gem5ImageUnpacker>();
}

}  // namespace cuttlefish
