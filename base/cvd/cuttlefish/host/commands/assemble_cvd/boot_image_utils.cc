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

#include "host/commands/assemble_cvd/boot_image_utils.h"

#include <string.h>
#include <unistd.h>

#include <fstream>
#include <memory>
#include <regex>
#include <string>

#include <android-base/logging.h>
#include <android-base/strings.h>

#include "android-base/scopeguard.h"
#include "common/libs/fs/shared_fd.h"
#include "common/libs/utils/files.h"
#include "common/libs/utils/result.h"
#include "common/libs/utils/subprocess.h"
#include "host/libs/avb/avb.h"
#include "host/libs/config/config_utils.h"
#include "host/libs/config/cuttlefish_config.h"
#include "host/libs/config/known_paths.h"

namespace cuttlefish {
namespace {

constexpr char TMP_EXTENSION[] = ".tmp";
constexpr char kCpioExt[] = ".cpio";
constexpr char TMP_RD_DIR[] = "stripped_ramdisk_dir";
constexpr char STRIPPED_RD[] = "stripped_ramdisk";
constexpr char kConcatenatedVendorRamdisk[] = "concatenated_vendor_ramdisk";

void RunMkBootFs(const std::string& input_dir, const std::string& output) {
  SharedFD output_fd = SharedFD::Open(output, O_CREAT | O_RDWR | O_TRUNC, 0644);
  CHECK(output_fd->IsOpen()) << output_fd->StrError();

  int success = Command(HostBinaryPath("mkbootfs"))
                    .AddParameter(input_dir)
                    .RedirectStdIO(Subprocess::StdIOChannel::kStdOut, output_fd)
                    .Start()
                    .Wait();
  CHECK_EQ(success, 0) << "`mkbootfs` failed.";
}

void RunLz4(const std::string& input, const std::string& output) {
  SharedFD output_fd = SharedFD::Open(output, O_CREAT | O_RDWR | O_TRUNC, 0644);
  CHECK(output_fd->IsOpen()) << output_fd->StrError();
  int success = Command("lz4")
                    .AddParameter("-c")
                    .AddParameter("-l")
                    .AddParameter("-12")
                    .AddParameter("--favor-decSpeed")
                    .AddParameter(input)
                    .RedirectStdIO(Subprocess::StdIOChannel::kStdOut, output_fd)
                    .Start()
                    .Wait();
  CHECK_EQ(success, 0) << "`lz4` failed to transform '" << input << "' to '"
                       << output << "'";
}

std::string ExtractValue(const std::string& dictionary, const std::string& key) {
  std::size_t index = dictionary.find(key);
  if (index != std::string::npos) {
    std::size_t end_index = dictionary.find('\n', index + key.length());
    if (end_index != std::string::npos) {
      return dictionary.substr(index + key.length(),
          end_index - index - key.length());
    }
  }
  return "";
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
    LOG(DEBUG) << "Updated " << current_file;
  } else {
    LOG(DEBUG) << "Didn't update " << current_file;
    RemoveFile(tmp_file);
  }

  return true;
}

void RepackVendorRamdisk(const std::string& kernel_modules_ramdisk_path,
                         const std::string& original_ramdisk_path,
                         const std::string& new_ramdisk_path,
                         const std::string& build_dir) {
  int success = 0;
  const std::string ramdisk_stage_dir = build_dir + "/" + TMP_RD_DIR;
  UnpackRamdisk(original_ramdisk_path, ramdisk_stage_dir);

  success = Execute({"rm", "-rf", ramdisk_stage_dir + "/lib/modules"});
  CHECK(success == 0) << "Could not rmdir \"lib/modules\" in TMP_RD_DIR. "
                      << "Exited with status " << success;

  const std::string stripped_ramdisk_path = build_dir + "/" + STRIPPED_RD;

  PackRamdisk(ramdisk_stage_dir, stripped_ramdisk_path);

  // Concatenates the stripped ramdisk and input ramdisk and places the result at new_ramdisk_path
  std::ofstream final_rd(new_ramdisk_path, std::ios_base::binary | std::ios_base::trunc);
  std::ifstream ramdisk_a(stripped_ramdisk_path, std::ios_base::binary);
  std::ifstream ramdisk_b(kernel_modules_ramdisk_path, std::ios_base::binary);
  final_rd << ramdisk_a.rdbuf() << ramdisk_b.rdbuf();
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

void PackRamdisk(const std::string& ramdisk_stage_dir,
                 const std::string& output_ramdisk) {
  RunMkBootFs(ramdisk_stage_dir, output_ramdisk + kCpioExt);
  RunLz4(output_ramdisk + kCpioExt, output_ramdisk);
}

void UnpackRamdisk(const std::string& original_ramdisk_path,
                   const std::string& ramdisk_stage_dir) {
  int success = 0;
  if (IsCpioArchive(original_ramdisk_path)) {
    CHECK(Copy(original_ramdisk_path, original_ramdisk_path + kCpioExt))
        << "failed to copy " << original_ramdisk_path << " to "
        << original_ramdisk_path + kCpioExt;
  } else {
    SharedFD output_fd = SharedFD::Open(original_ramdisk_path + kCpioExt,
                                        O_CREAT | O_RDWR | O_TRUNC, 0644);
    CHECK(output_fd->IsOpen()) << output_fd->StrError();

    success = Command("lz4")
                  .AddParameter("-c")
                  .AddParameter("-d")
                  .AddParameter("-l")
                  .AddParameter(original_ramdisk_path)
                  .RedirectStdIO(Subprocess::StdIOChannel::kStdOut, output_fd)
                  .Start()
                  .Wait();
    CHECK_EQ(success, 0) << "Unable to run lz4 on file '"
                         << original_ramdisk_path << "'.";
  }
  const auto ret = EnsureDirectoryExists(ramdisk_stage_dir);
  CHECK(ret.ok()) << ret.error().FormatForEnv();

  SharedFD input = SharedFD::Open(original_ramdisk_path + kCpioExt, O_RDONLY);
  int cpio_status;
  do {
    LOG(ERROR) << "Running";
    cpio_status = Command("cpio")
                      .AddParameter("-idu")
                      .SetWorkingDirectory(ramdisk_stage_dir)
                      .RedirectStdIO(Subprocess::StdIOChannel::kStdIn, input)
                      .Start()
                      .Wait();
  } while (cpio_status == 0);
}

bool GetAvbMetadataFromBootImage(const std::string& boot_image_path,
                                 const std::string& unpack_dir) {
  std::unique_ptr<Avb> avbtool = GetDefaultAvb();
  Result<void> result =
      avbtool->WriteInfoImage(boot_image_path, unpack_dir + "/boot_params");
  if (!result.ok()) {
    LOG(ERROR) << result.error().Trace();
    return false;
  }
  return true;
}

Result<void> UnpackBootImage(const std::string& boot_image_path,
                             const std::string& unpack_dir) {
  SharedFD output_file = SharedFD::Creat(unpack_dir + "/boot_params", 0666);
  CF_EXPECTF(output_file->IsOpen(),
             "Unable to create intermediate boot params file: '{}'",
             output_file->StrError());

  Command unpack_cmd =
      Command(HostBinaryPath("unpack_bootimg"))
          .AddParameter("--boot_img")
          .AddParameter(boot_image_path)
          .AddParameter("--out")
          .AddParameter(unpack_dir)
          .RedirectStdIO(Subprocess::StdIOChannel::kStdOut, output_file);

  CF_EXPECT_EQ(unpack_cmd.Start().Wait(), 0, "Unable to run unpack_bootimg.");

  return {};
}

bool UnpackVendorBootImageIfNotUnpacked(
    const std::string& vendor_boot_image_path, const std::string& unpack_dir) {
  // the vendor boot params file is created during the first unpack. If it's
  // already there, a unpack has occurred and there's no need to repeat the
  // process.
  if (FileExists(unpack_dir + "/vendor_boot_params")) {
    return true;
  }

  auto unpack_path = HostBinaryPath("unpack_bootimg");
  Command unpack_cmd(unpack_path);
  unpack_cmd.AddParameter("--boot_img");
  unpack_cmd.AddParameter(vendor_boot_image_path);
  unpack_cmd.AddParameter("--out");
  unpack_cmd.AddParameter(unpack_dir);
  auto output_file = SharedFD::Creat(unpack_dir + "/vendor_boot_params", 0666);
  if (!output_file->IsOpen()) {
    LOG(ERROR) << "Unable to create intermediate vendor boot params file: "
               << output_file->StrError();
    return false;
  }
  unpack_cmd.RedirectStdIO(Subprocess::StdIOChannel::kStdOut, output_file);
  int success = unpack_cmd.Start().Wait();
  if (success != 0) {
    LOG(ERROR) << "Unable to run unpack_bootimg. Exited with status " << success;
    return false;
  }

  // Concatenates all vendor ramdisk into one single ramdisk.
  std::string concat_file_path = unpack_dir + "/" + kConcatenatedVendorRamdisk;
  SharedFD concat_file = SharedFD::Creat(concat_file_path, 0666);
  if (!concat_file->IsOpen()) {
    LOG(ERROR) << "Unable to create concatenated vendor ramdisk file: "
               << concat_file->StrError();
    return false;
  }

  Result<std::vector<std::string>> unpack_files = DirectoryContents(unpack_dir);
  if (!unpack_files.ok()) {
    LOG(ERROR) << "No unpacked files: " << unpack_files.error().FormatForEnv();
    return false;
  }
  for (const std::string& unpacked : *unpack_files) {
    LOG(ERROR) << "acs: " << unpacked;
    if (!android::base::StartsWith(unpacked, "vendor_ramdisk")) {
      continue;
    }
    std::string input_path = unpack_dir + "/" + unpacked;
    SharedFD input = SharedFD::Open(input_path, O_RDONLY);
    if (!input->IsOpen()) {
      LOG(ERROR) << "Failed to open '" << input_path << ": "
                 << input->StrError();
      return false;
    }
    if (!concat_file->CopyAllFrom(*input)) {
      LOG(ERROR) << "Failed to copy from '" << input_path << "' to '"
                 << concat_file_path << "'";
      return false;
    }
  }
  return true;
}

Result<void> RepackBootImage(const Avb& avb,
                             const std::string& new_kernel_path,
                             const std::string& boot_image_path,
                             const std::string& new_boot_image_path,
                             const std::string& build_dir) {
  CF_EXPECT(UnpackBootImage(boot_image_path, build_dir));

  std::string boot_params = ReadFile(build_dir + "/boot_params");
  auto kernel_cmdline = ExtractValue(boot_params, "command line args: ");
  LOG(DEBUG) << "Cmdline from boot image is " << kernel_cmdline;

  auto tmp_boot_image_path = new_boot_image_path + TMP_EXTENSION;
  auto repack_path = HostBinaryPath("mkbootimg");
  Command repack_cmd(repack_path);
  repack_cmd.AddParameter("--kernel");
  repack_cmd.AddParameter(new_kernel_path);
  repack_cmd.AddParameter("--ramdisk");
  repack_cmd.AddParameter(build_dir + "/ramdisk");
  repack_cmd.AddParameter("--header_version");
  repack_cmd.AddParameter("4");
  repack_cmd.AddParameter("--cmdline");
  repack_cmd.AddParameter(kernel_cmdline);
  repack_cmd.AddParameter("-o");
  repack_cmd.AddParameter(tmp_boot_image_path);
  int result = repack_cmd.Start().Wait();
  CF_EXPECT(result == 0, "Unable to run mkbootimg. Exited with status " << result);

  if (FileSize(tmp_boot_image_path) <= FileSize(boot_image_path)) {
    CF_EXPECT(avb.AddHashFooter(tmp_boot_image_path, "boot", FileSize(boot_image_path)));
  } else {
    CF_EXPECT(avb.AddHashFooter(tmp_boot_image_path, "boot", 0));
  }
  CF_EXPECT(DeleteTmpFileIfNotChanged(tmp_boot_image_path, new_boot_image_path));

  return {};
}

bool RepackVendorBootImage(const std::string& new_ramdisk,
                           const std::string& vendor_boot_image_path,
                           const std::string& new_vendor_boot_image_path,
                           const std::string& unpack_dir,
                           bool bootconfig_supported) {
  if (UnpackVendorBootImageIfNotUnpacked(vendor_boot_image_path, unpack_dir) ==
      false) {
    return false;
  }

  std::string ramdisk_path;
  if (new_ramdisk.size()) {
    ramdisk_path = unpack_dir + "/vendor_ramdisk_repacked";
    if (!FileExists(ramdisk_path)) {
      RepackVendorRamdisk(new_ramdisk,
                          unpack_dir + "/" + kConcatenatedVendorRamdisk,
                          ramdisk_path, unpack_dir);
    }
  } else {
    ramdisk_path = unpack_dir + "/" + kConcatenatedVendorRamdisk;
  }

  std::string bootconfig = ReadFile(unpack_dir + "/bootconfig");
  LOG(DEBUG) << "Bootconfig parameters from vendor boot image are "
             << bootconfig;
  std::string vendor_boot_params = ReadFile(unpack_dir + "/vendor_boot_params");
  auto kernel_cmdline =
      ExtractValue(vendor_boot_params, "vendor command line args: ") +
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
  LOG(DEBUG) << "Cmdline from vendor boot image is " << kernel_cmdline;

  auto tmp_vendor_boot_image_path = new_vendor_boot_image_path + TMP_EXTENSION;
  auto repack_path = HostBinaryPath("mkbootimg");
  Command repack_cmd(repack_path);
  repack_cmd.AddParameter("--vendor_ramdisk");
  repack_cmd.AddParameter(ramdisk_path);
  repack_cmd.AddParameter("--header_version");
  repack_cmd.AddParameter("4");
  repack_cmd.AddParameter("--vendor_cmdline");
  repack_cmd.AddParameter(kernel_cmdline);
  repack_cmd.AddParameter("--vendor_boot");
  repack_cmd.AddParameter(tmp_vendor_boot_image_path);
  repack_cmd.AddParameter("--dtb");
  repack_cmd.AddParameter(unpack_dir + "/dtb");
  if (bootconfig_supported) {
    repack_cmd.AddParameter("--vendor_bootconfig");
    repack_cmd.AddParameter(unpack_dir + "/bootconfig");
  }

  int success = repack_cmd.Start().Wait();
  if (success != 0) {
    LOG(ERROR) << "Unable to run mkbootimg. Exited with status " << success;
    return false;
  }

  auto avbtool = Avb(AvbToolBinary());
  Result<void> result =
      avbtool.AddHashFooter(tmp_vendor_boot_image_path, "vendor_boot",
                            FileSize(vendor_boot_image_path));
  if (!result.ok()) {
    LOG(ERROR) << result.error().Trace();
    return false;
  }

  return DeleteTmpFileIfNotChanged(tmp_vendor_boot_image_path, new_vendor_boot_image_path);
}

bool RepackVendorBootImageWithEmptyRamdisk(
    const std::string& vendor_boot_image_path,
    const std::string& new_vendor_boot_image_path,
    const std::string& unpack_dir, bool bootconfig_supported) {
  auto empty_ramdisk_file =
      SharedFD::Creat(unpack_dir + "/empty_ramdisk", 0666);
  return RepackVendorBootImage(
      unpack_dir + "/empty_ramdisk", vendor_boot_image_path,
      new_vendor_boot_image_path, unpack_dir, bootconfig_supported);
}

void RepackGem5BootImage(const std::string& initrd_path,
                         const std::string& bootconfig_path,
                         const std::string& unpack_dir,
                         const std::string& input_ramdisk_path) {
  // Simulate per-instance what the bootloader would usually do
  // Since on other devices this runs every time, just do it here every time
  std::ofstream final_rd(initrd_path,
                         std::ios_base::binary | std::ios_base::trunc);

  std::ifstream boot_ramdisk(unpack_dir + "/ramdisk",
                             std::ios_base::binary);
  std::string new_ramdisk_path = unpack_dir + "/vendor_ramdisk_repacked";
  // Test to make sure new ramdisk hasn't already been repacked if input ramdisk is provided
  if (FileExists(input_ramdisk_path) && !FileExists(new_ramdisk_path)) {
    RepackVendorRamdisk(input_ramdisk_path,
                        unpack_dir + "/" + kConcatenatedVendorRamdisk,
                        new_ramdisk_path, unpack_dir);
  }
  std::ifstream vendor_boot_ramdisk(FileExists(new_ramdisk_path) ? new_ramdisk_path : unpack_dir +
                                    "/concatenated_vendor_ramdisk",
                                    std::ios_base::binary);

  std::ifstream vendor_boot_bootconfig(unpack_dir + "/bootconfig",
                                       std::ios_base::binary |
                                       std::ios_base::ate);

  auto vb_size = vendor_boot_bootconfig.tellg();
  vendor_boot_bootconfig.seekg(0);

  std::ifstream persistent_bootconfig(bootconfig_path,
                                      std::ios_base::binary |
                                      std::ios_base::ate);

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
}

// TODO(290586882) switch this function to rely on avb footers instead of
// the os version field in the boot image header.
// https://source.android.com/docs/core/architecture/bootloader/boot-image-header
Result<std::string> ReadAndroidVersionFromBootImage(
    const std::string& temp_dir_parent, const std::string& boot_image_path) {
  std::string tmp_dir = temp_dir_parent + "/XXXXXXX";
  if (!mkdtemp(tmp_dir.data())) {
    return CF_ERR("boot image unpack dir could not be created");
  }
  android::base::ScopeGuard delete_dir([tmp_dir]() {
    Result<void> remove_res = RecursivelyRemoveDirectory(tmp_dir);
    if (!remove_res.ok()) {
      LOG(ERROR) << "Failed to delete temp dir '" << tmp_dir << '"';
      LOG(ERROR) << remove_res.error().FormatForEnv();
    }
  });

  CF_EXPECTF(GetAvbMetadataFromBootImage(boot_image_path, tmp_dir),
             "'{}' boot image unpack into '{}' failed", boot_image_path,
             tmp_dir);

  std::string boot_params = ReadFile(tmp_dir + "/boot_params");

  std::string os_version =
      ExtractValue(boot_params, "Prop: com.android.build.boot.os_version -> ");
  // if the OS version is "None", or the prop does not exist, it wasn't set
  // when the boot image was made.
  if (os_version == "None" || os_version.empty()) {
    LOG(INFO) << "Could not extract os version from " << boot_image_path
              << ". Defaulting to 0.0.0.";
    return "0.0.0";
  }

  // os_version returned above is surrounded by single quotes. Removing the
  // single quotes.
  os_version.erase(remove(os_version.begin(), os_version.end(), '\''),
                   os_version.end());

  std::regex re("[1-9][0-9]*([.][0-9]+)*");
  CF_EXPECT(std::regex_match(os_version, re), "Version string is not a valid version \"" + os_version + "\"");
  return os_version;
}
} // namespace cuttlefish
