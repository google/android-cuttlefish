/*
 * Copyright (C) 2020 The Android Open Source Project
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

#include "cuttlefish/host/commands/assemble_cvd/boot_image_utils.h"

#include <stddef.h>
#include <string.h>
#include <unistd.h>

#include <fstream>
#include <optional>
#include <regex>
#include <string>

#include "absl/log/check.h"
#include "absl/log/log.h"
#include "android-base/strings.h"

#include "cuttlefish/common/libs/fs/shared_fd.h"
#include "cuttlefish/common/libs/utils/files.h"
#include "cuttlefish/common/libs/utils/subprocess.h"
#include "cuttlefish/host/commands/assemble_cvd/boot_image/boot_image.h"
#include "cuttlefish/host/commands/assemble_cvd/boot_image/boot_image_builder.h"
#include "cuttlefish/host/commands/assemble_cvd/boot_image/vendor_boot_image.h"
#include "cuttlefish/host/commands/assemble_cvd/boot_image/vendor_boot_image_builder.h"
#include "cuttlefish/host/libs/avb/avb.h"
#include "cuttlefish/host/libs/avb/parser.h"
#include "cuttlefish/host/libs/config/config_utils.h"
#include "cuttlefish/host/libs/config/known_paths.h"
#include "cuttlefish/io/chroot.h"
#include "cuttlefish/io/concat.h"
#include "cuttlefish/io/copy.h"
#include "cuttlefish/io/io.h"
#include "cuttlefish/io/length.h"
#include "cuttlefish/io/lz4_legacy.h"
#include "cuttlefish/io/native_filesystem.h"
#include "cuttlefish/io/shared_fd.h"
#include "cuttlefish/io/string.h"
#include "cuttlefish/io/write_exact.h"
#include "cuttlefish/result/result.h"

namespace cuttlefish {
namespace {

constexpr char TMP_EXTENSION[] = ".tmp";
constexpr char kCpioExt[] = ".cpio";
constexpr char TMP_RD_DIR[] = "stripped_ramdisk_dir";
constexpr char STRIPPED_RD[] = "stripped_ramdisk";
constexpr char kConcatenatedVendorRamdisk[] = "concatenated_vendor_ramdisk";

Result<void> RunMkBootFs(const std::string& input_dir,
                         const std::string& output) {
  SharedFD output_fd = SharedFD::Open(output, O_CREAT | O_RDWR | O_TRUNC, 0644);
  CF_EXPECTF(output_fd->IsOpen(), "Failed to open '{}': '{}'", output,
             output_fd->StrError());

  int success = Command(HostBinaryPath("mkbootfs"))
                    .AddParameter(input_dir)
                    .RedirectStdIO(Subprocess::StdIOChannel::kStdOut, output_fd)
                    .Start()
                    .Wait();
  CF_EXPECT_EQ(success, 0, "`mkbootfs` failed.");
  return {};
}

Result<void> RunLz4(const std::string& input, const std::string& output) {
  NativeFilesystem fs;
  std::unique_ptr<Reader> input_reader = CF_EXPECT(fs.OpenReadOnly(input));
  (void)fs.DeleteFile(output);
  std::unique_ptr<Writer> output_writer = CF_EXPECT(fs.CreateFile(output));
  std::unique_ptr<Writer> lz4_writer =
      CF_EXPECT(Lz4LegacyWriter(std::move(output_writer)));
  std::string input_data = CF_EXPECT(ReadToString(*input_reader));
  CF_EXPECT(WriteExact(*lz4_writer, input_data.data(), input_data.size()));
  return {};
}

// Though it is just as fast to overwrite the existing boot images with the newly generated ones,
// the cuttlefish composite disk generator checks the age of each of the components and
// regenerates the disk outright IF any one of the components is younger/newer than the current
// composite disk. If this file overwrite occurs, that condition is fulfilled. This action then
// causes data in the userdata partition from previous boots to be lost (which is not expected by
// the user if they've been booting the same kernel/ramdisk combination repeatedly).
// Consequently, the file is checked for differences and ONLY overwritten if there is a diff.
bool DeleteTmpFileIfNotChanged(const std::string& tmp_file, const std::string& current_file) {
  if (!FileExists(current_file) ||
      ReadFile(current_file) != ReadFile(tmp_file)) {
    if (!RenameFile(tmp_file, current_file).ok()) {
      LOG(ERROR) << "Unable to delete " << current_file;
      return false;
    }
    VLOG(0) << "Updated " << current_file;
  } else {
    VLOG(0) << "Didn't update " << current_file;
    if (Result<void> res = RemoveFile(tmp_file); !res.ok()) {
      LOG(ERROR) << res.error();
    }
  }

  return true;
}

Result<void> RepackVendorRamdisk(const std::string& kernel_modules_ramdisk_path,
                                 const std::string& original_ramdisk_path,
                                 const std::string& new_ramdisk_path,
                                 const std::string& build_dir) {
  const std::string ramdisk_stage_dir = build_dir + "/" + TMP_RD_DIR;
  CF_EXPECT(UnpackRamdisk(original_ramdisk_path, ramdisk_stage_dir));

  int success = Execute({"rm", "-rf", ramdisk_stage_dir + "/lib/modules"});
  CF_EXPECT_EQ(success, 0, "Could not rmdir 'lib/modules' in TMP_RD_DIR. ");

  const std::string stripped_ramdisk_path = build_dir + "/" + STRIPPED_RD;

  CF_EXPECT(PackRamdisk(ramdisk_stage_dir, stripped_ramdisk_path));

  // Concatenates the stripped ramdisk and input ramdisk and places the result at new_ramdisk_path
  std::ofstream final_rd(new_ramdisk_path, std::ios_base::binary | std::ios_base::trunc);
  std::ifstream ramdisk_a(stripped_ramdisk_path, std::ios_base::binary);
  std::ifstream ramdisk_b(kernel_modules_ramdisk_path, std::ios_base::binary);
  final_rd << ramdisk_a.rdbuf() << ramdisk_b.rdbuf();

  return {};
}

bool IsCpioArchive(const std::string& path) {
  static constexpr std::string_view CPIO_MAGIC = "070701";
  auto fd = SharedFD::Open(path, O_RDONLY);
  std::array<char, CPIO_MAGIC.size()> buf{};
  if (fd->Read(buf.data(), buf.size()) != CPIO_MAGIC.size()) {
    return false;
  }
  return memcmp(buf.data(), CPIO_MAGIC.data(), CPIO_MAGIC.size()) == 0;
}

}  // namespace

Result<void> PackRamdisk(const std::string& ramdisk_stage_dir,
                         const std::string& output_ramdisk) {
  CF_EXPECT(RunMkBootFs(ramdisk_stage_dir, output_ramdisk + kCpioExt));
  CF_EXPECT(RunLz4(output_ramdisk + kCpioExt, output_ramdisk));
  return {};
}

Result<void> UnpackRamdisk(const std::string& original_ramdisk_path,
                           const std::string& ramdisk_stage_dir) {
  NativeFilesystem fs;
  std::unique_ptr<Reader> ramdisk_input = CF_EXPECT(fs.OpenReadOnly(original_ramdisk_path));
  CF_EXPECT(ramdisk_input.get());

  const std::string output_path = original_ramdisk_path + kCpioExt;
  (void) fs.DeleteFile(output_path);
  std::unique_ptr<WriterSeeker> output = CF_EXPECT(fs.CreateFile(output_path));

  if (!IsCpioArchive(original_ramdisk_path)) {
    ramdisk_input = CF_EXPECT(Lz4LegacyReader(std::move(ramdisk_input)));
  }
  CF_EXPECTF(Copy(*ramdisk_input, *output), "Failed to copy '{}' to '{}'",
             original_ramdisk_path, output_path);

  CF_EXPECT(EnsureDirectoryExists(ramdisk_stage_dir));

  SharedFD input = SharedFD::Open(original_ramdisk_path + kCpioExt, O_RDONLY);
  int cpio_status;
  do {
    LOG(ERROR) << "Running";
    cpio_status = Command(CpioBinary())
                      .AddParameter("-idu")
                      .SetWorkingDirectory(ramdisk_stage_dir)
                      .RedirectStdIO(Subprocess::StdIOChannel::kStdIn, input)
                      .Start()
                      .Wait();
  } while (cpio_status == 0);
  return {};
}

Result<void> UnpackBootImage(const std::string& boot_image_path,
                             const std::string& unpack_dir) {
  NativeFilesystem native_filesystem;

  std::unique_ptr<ReaderSeeker> boot_image_reader =
      CF_EXPECT(native_filesystem.OpenReadOnly(boot_image_path));
  BootImage boot_image =
      CF_EXPECT(BootImage::Read(std::move(boot_image_reader)));

  ChrootReadWriteFilesystem build_dir_chroot(native_filesystem,
                                             unpack_dir + "/");

  CF_EXPECT(boot_image.Unpack(build_dir_chroot));

  return {};
}

Result<VendorBootImage> UnpackVendorBootImageIfNotUnpacked(
    const std::string& vendor_boot_image_path, const std::string& unpack_dir) {
  SharedFD vendor_boot_fd = SharedFD::Open(vendor_boot_image_path, O_RDONLY);
  CF_EXPECTF(vendor_boot_fd->IsOpen(), "Failed to open '{}': '{}'",
             vendor_boot_image_path, vendor_boot_fd->StrError());

  VendorBootImage vendor_boot = CF_EXPECT(
      VendorBootImage::Read(std::make_unique<SharedFdIo>(vendor_boot_fd)));
  // The ramdisk file is created during the first unpack. If it's already there,
  // a unpack has occurred and there's no need to repeat the process.
  std::string concat_file_path = unpack_dir + "/" + kConcatenatedVendorRamdisk;
  if (FileExists(concat_file_path)) {
    return vendor_boot;
  }

  // Concatenates all vendor ramdisk into one single ramdisk.
  SharedFD concat_file = SharedFD::Creat(concat_file_path, 0666);
  CF_EXPECTF(concat_file->IsOpen(),
             "Unable to create concatenated vendor ramdisk file: '{}'",
             concat_file->StrError());
  SharedFdIo concat_file_io(concat_file);
  ReadWindowView ramdisk_in = vendor_boot.VendorRamdisk();
  CF_EXPECT(Copy(ramdisk_in, concat_file_io));

  std::string dtb_path = unpack_dir + "/dtb";
  SharedFD dtb_fd = SharedFD::Creat(dtb_path, 0644);
  CF_EXPECTF(dtb_fd->IsOpen(), "Failed to open '{}': '{}'", dtb_path,
             dtb_fd->StrError());
  SharedFdIo dtb_io(dtb_fd);
  ReadWindowView dtb_in = vendor_boot.Dtb();
  CF_EXPECT(Copy(dtb_in, dtb_io));

  std::string bootconfig_path = unpack_dir + "/bootconfig";
  SharedFD bootconfig_fd = SharedFD::Creat(bootconfig_path, 0644);
  CF_EXPECTF(bootconfig_fd->IsOpen(), "Failed to open '{}': '{}'",
             bootconfig_path, bootconfig_fd->StrError());
  SharedFdIo bootconfig_io(bootconfig_fd);
  std::optional<ReadWindowView> bootconfig_in = vendor_boot.Bootconfig();
  if (bootconfig_in.has_value()) {
    CF_EXPECT(Copy(*bootconfig_in, bootconfig_io));
  }

  return vendor_boot;
}

Result<void> RepackBootImage(const std::string& new_kernel_path,
                             const std::string& boot_image_path,
                             const std::string& new_boot_image_path) {
  NativeFilesystem native_filesystem;

  std::unique_ptr<ReaderSeeker> boot_image_reader =
      CF_EXPECT(native_filesystem.OpenReadOnly(boot_image_path));
  BootImage boot_image =
      CF_EXPECT(BootImage::Read(std::move(boot_image_reader)));

  BootImageBuilder builder =
      BootImageBuilder()
          .OsVersion(boot_image.OsVersion())
          .KernelCommandLine(boot_image.KernelCommandLine())
          .Kernel(CF_EXPECT(native_filesystem.OpenReadOnly(new_kernel_path)))
          .Ramdisk(std::make_unique<ReadWindowView>(boot_image.Ramdisk()));

  if (std::optional<ReadWindowView> sig = boot_image.Signature(); sig) {
    builder.Signature(std::make_unique<ReadWindowView>(*sig));
  }
  ConcatReaderSeeker new_boot_image = CF_EXPECT(builder.BuildV4());
  uint64_t new_boot_image_size = CF_EXPECT(Length(new_boot_image));

  std::string tmp_boot_image_path = new_boot_image_path + TMP_EXTENSION;
  (void)native_filesystem.DeleteFile(tmp_boot_image_path);
  std::unique_ptr<ReaderWriterSeeker> tmp_boot_image =
      CF_EXPECT(native_filesystem.CreateFile(tmp_boot_image_path));
  CF_EXPECT(tmp_boot_image.get());
  CF_EXPECT(Copy(new_boot_image, *tmp_boot_image));

  if (new_boot_image_size <= FileSize(boot_image_path)) {
    CF_EXPECT(Avb().AddHashFooter(tmp_boot_image_path, "boot",
                                  FileSize(boot_image_path)));
  } else {
    CF_EXPECT(Avb().AddHashFooter(tmp_boot_image_path, "boot", 0));
  }
  CF_EXPECT(DeleteTmpFileIfNotChanged(tmp_boot_image_path, new_boot_image_path));

  return {};
}

Result<void> RepackVendorBootImage(
    const std::string& new_ramdisk, const std::string& vendor_boot_image_path,
    const std::string& new_vendor_boot_image_path,
    const std::string& unpack_dir, bool bootconfig_supported) {
  VendorBootImage unpack = CF_EXPECT(
      UnpackVendorBootImageIfNotUnpacked(vendor_boot_image_path, unpack_dir));

  std::string ramdisk_path;
  if (!new_ramdisk.empty()) {
    ramdisk_path = unpack_dir + "/vendor_ramdisk_repacked";
    if (!FileExists(ramdisk_path)) {
      CF_EXPECT(RepackVendorRamdisk(
          new_ramdisk, unpack_dir + "/" + kConcatenatedVendorRamdisk,
          ramdisk_path, unpack_dir));
    }
  } else {
    ramdisk_path = unpack_dir + "/" + kConcatenatedVendorRamdisk;
  }

  std::string bootconfig = ReadFile(unpack_dir + "/bootconfig");
  VLOG(0) << "Bootconfig parameters from vendor boot image are " << bootconfig;
  std::string kernel_cmdline =
      unpack.KernelCommandLine() +
      (bootconfig_supported
           ? ""
           : " " + android::base::StringReplace(bootconfig, "\n", " ", true));
  if (!bootconfig_supported) {
    // TODO(b/182417593): Until we pass the module parameters through
    // modules.options, we pass them through bootconfig using
    // 'kernel.<key>=<value>' But if we don't support bootconfig, we need to
    // rename them back to the old cmdline version
    kernel_cmdline = android::base::StringReplace(
        kernel_cmdline, " kernel.", " ", true);
  }
  VLOG(0) << "Cmdline from vendor boot image is " << kernel_cmdline;

  auto tmp_vendor_boot_image_path = new_vendor_boot_image_path + TMP_EXTENSION;

  NativeFilesystem fs;

  VendorBootImageBuilder builder =
      VendorBootImageBuilder()
          .PageSize(unpack.PageSize())
          .KernelAddr(unpack.KernelAddr())
          .RamdiskAddr(unpack.RamdiskAddr())
          .VendorRamdisk(CF_EXPECT(fs.OpenReadOnly(ramdisk_path)))
          .KernelCommandLine(kernel_cmdline)
          .TagsAddr(unpack.TagsAddr())
          .Name(unpack.Name())
          .Dtb(CF_EXPECT(fs.OpenReadOnly(unpack_dir + "/dtb")))
          .DtbAddr(unpack.DtbAddr());
  if (bootconfig_supported) {
    builder.Bootconfig(CF_EXPECT(fs.OpenReadOnly(unpack_dir + "/bootconfig")));
  }

  ConcatReaderSeeker new_vendor_boot_image = CF_EXPECT(builder.BuildV4());

  (void)fs.DeleteFile(tmp_vendor_boot_image_path);
  std::unique_ptr<Writer> tmp_vendor_boot_image_file =
      CF_EXPECT(fs.CreateFile(tmp_vendor_boot_image_path));

  CF_EXPECT(Copy(new_vendor_boot_image, *tmp_vendor_boot_image_file));

  CF_EXPECT(Avb().AddHashFooter(tmp_vendor_boot_image_path, "vendor_boot",
                                FileSize(vendor_boot_image_path)));

  CF_EXPECT(DeleteTmpFileIfNotChanged(tmp_vendor_boot_image_path,
                                      new_vendor_boot_image_path));

  return {};
}

Result<void> RepackVendorBootImageWithEmptyRamdisk(
    const std::string& vendor_boot_image_path,
    const std::string& new_vendor_boot_image_path,
    const std::string& unpack_dir, bool bootconfig_supported) {
  std::string empty_ramdisk_path = unpack_dir + "/empty_ramdisk";
  SharedFD empty_ramdisk = SharedFD::Creat(empty_ramdisk_path, 0666);
  CF_EXPECTF(empty_ramdisk->IsOpen(), "Failed to open '{}': '{}'",
             empty_ramdisk_path, empty_ramdisk->StrError());
  CF_EXPECT(RepackVendorBootImage(empty_ramdisk_path, vendor_boot_image_path,
                                  new_vendor_boot_image_path, unpack_dir,
                                  bootconfig_supported));
  return {};
}

Result<void> RepackGem5BootImage(
    const std::string& initrd_path,
    const std::optional<BootConfigPartition>& bootconfig_partition,
    const std::string& unpack_dir, const std::string& input_ramdisk_path) {
  // Simulate per-instance what the bootloader would usually do
  // Since on other devices this runs every time, just do it here every time
  std::ofstream final_rd(initrd_path,
                         std::ios_base::binary | std::ios_base::trunc);

  std::ifstream boot_ramdisk(unpack_dir + "/ramdisk",
                             std::ios_base::binary);
  std::string new_ramdisk_path = unpack_dir + "/vendor_ramdisk_repacked";
  // Test to make sure new ramdisk hasn't already been repacked if input ramdisk is provided
  if (FileExists(input_ramdisk_path) && !FileExists(new_ramdisk_path)) {
    CF_EXPECT(RepackVendorRamdisk(input_ramdisk_path,
                                  unpack_dir + "/" + kConcatenatedVendorRamdisk,
                                  new_ramdisk_path, unpack_dir));
  }
  std::ifstream vendor_boot_ramdisk(FileExists(new_ramdisk_path) ? new_ramdisk_path : unpack_dir +
                                    "/concatenated_vendor_ramdisk",
                                    std::ios_base::binary);

  std::ifstream vendor_boot_bootconfig(unpack_dir + "/bootconfig",
                                       std::ios_base::binary |
                                       std::ios_base::ate);

  auto vb_size = vendor_boot_bootconfig.tellg();
  vendor_boot_bootconfig.seekg(0);

  std::ifstream persistent_bootconfig =
      bootconfig_partition
          ? std::ifstream(bootconfig_partition->Path(),
                          std::ios_base::binary | std::ios_base::ate)
          : std::ifstream();

  auto pb_size = persistent_bootconfig.tellg();
  persistent_bootconfig.seekg(0);

  // Build the bootconfig string, trim it, and write the length, checksum
  // and trailer bytes

  std::string bootconfig =
    "androidboot.slot_suffix=_a\n"
    "androidboot.force_normal_boot=1\n"
    "androidboot.verifiedbootstate=orange\n";
  auto bootconfig_size = bootconfig.size();
  bootconfig.resize(bootconfig_size + (uint64_t)(vb_size + pb_size), '\0');
  vendor_boot_bootconfig.read(&bootconfig[bootconfig_size], vb_size);
  persistent_bootconfig.read(&bootconfig[bootconfig_size + vb_size], pb_size);
  // Trim the block size padding from the persistent bootconfig
  bootconfig.erase(bootconfig.find_last_not_of('\0'));

  // Write out the ramdisks and bootconfig blocks
  final_rd << boot_ramdisk.rdbuf() << vendor_boot_ramdisk.rdbuf()
           << bootconfig;

  // Append bootconfig length
  bootconfig_size = bootconfig.size();
  final_rd.write(reinterpret_cast<const char *>(&bootconfig_size),
                 sizeof(uint32_t));

  // Append bootconfig checksum
  uint32_t bootconfig_csum = 0;
  for (auto i = 0; i < bootconfig_size; i++) {
    bootconfig_csum += bootconfig[i];
  }
  final_rd.write(reinterpret_cast<const char *>(&bootconfig_csum),
                 sizeof(uint32_t));

  // Append bootconfig trailer
  final_rd << "#BOOTCONFIG\n";
  final_rd.close();

  return {};
}

Result<std::string> ReadAndroidVersionFromBootImage(
    const std::string& boot_image_path) {
  NativeFilesystem fs;
  std::unique_ptr<ReaderSeeker> boot_image =
      CF_EXPECT(fs.OpenReadOnly(boot_image_path));
  CF_EXPECT(boot_image.get());

  AvbParser avb_parser = CF_EXPECT(AvbParser::Parse(*boot_image));

  Result<std::string> os_version =
      avb_parser.LookupProperty("com.android.build.boot.os_version");
  // if the OS version is "None", or the prop does not exist, it wasn't set
  // when the boot image was made.
  if (!os_version.ok() || *os_version == "None") {
    LOG(INFO) << "Could not extract os version from " << boot_image_path
              << ". Defaulting to 0.0.0.";
    return "0.0.0";
  }

  std::regex re("[1-9][0-9]*([.][0-9]+)*");
  CF_EXPECTF(std::regex_match(*os_version, re),
             "Version string is not a valid version: '{}'", *os_version);
  return os_version;
}
} // namespace cuttlefish
