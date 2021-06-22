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
#include "host/libs/config/cuttlefish_config.h"

#include <string.h>
#include <unistd.h>

#include <fstream>
#include <sstream>

#include <android-base/logging.h>
#include <android-base/strings.h>

#include "common/libs/utils/files.h"
#include "common/libs/utils/subprocess.h"

const char TMP_EXTENSION[] = ".tmp";
const char CPIO_EXT[] = ".cpio";
const char TMP_RD_DIR[] = "stripped_ramdisk_dir";
const char STRIPPED_RD[] = "stripped_ramdisk";
const char CONCATENATED_VENDOR_RAMDISK[] = "concatenated_vendor_ramdisk";
namespace cuttlefish {
namespace {
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
    if (!RenameFile(tmp_file, current_file)) {
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

bool UnpackBootImage(const std::string& boot_image_path,
                     const std::string& unpack_dir) {
  auto unpack_path = HostBinaryPath("unpack_bootimg");
  Command unpack_cmd(unpack_path);
  unpack_cmd.AddParameter("--boot_img");
  unpack_cmd.AddParameter(boot_image_path);
  unpack_cmd.AddParameter("--out");
  unpack_cmd.AddParameter(unpack_dir);

  auto output_file = SharedFD::Creat(unpack_dir + "/boot_params", 0666);
  if (!output_file->IsOpen()) {
    LOG(ERROR) << "Unable to create intermediate boot params file: "
               << output_file->StrError();
    return false;
  }
  unpack_cmd.RedirectStdIO(Subprocess::StdIOChannel::kStdOut, output_file);

  int success = unpack_cmd.Start().Wait();
  if (success != 0) {
    LOG(ERROR) << "Unable to run unpack_bootimg. Exited with status "
               << success;
    return false;
  }
  return true;
}

void RepackVendorRamdisk(const std::string& kernel_modules_ramdisk_path,
                         const std::string& original_ramdisk_path,
                         const std::string& new_ramdisk_path,
                         const std::string& build_dir) {
  int success = execute({"/bin/bash", "-c", HostBinaryPath("lz4") + " -c -d -l " +
                        original_ramdisk_path + " > " + original_ramdisk_path + CPIO_EXT});
  CHECK(success == 0) << "Unable to run lz4. Exited with status " << success;

  const std::string ramdisk_stage_dir = build_dir + "/" + TMP_RD_DIR;
  success =
      mkdir(ramdisk_stage_dir.c_str(), S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH);
  CHECK(success == 0) << "Could not mkdir \"" << ramdisk_stage_dir
                      << "\", error was " << strerror(errno);

  success = execute(
      {"/bin/bash", "-c",
       "(cd " + ramdisk_stage_dir + " && while " + HostBinaryPath("toybox") +
           " cpio -idu; do :; done) < " + original_ramdisk_path + CPIO_EXT});
  CHECK(success == 0) << "Unable to run cd or cpio. Exited with status "
                      << success;

  success = execute({"rm", "-rf", ramdisk_stage_dir + "/lib/modules"});
  CHECK(success == 0) << "Could not rmdir \"lib/modules\" in TMP_RD_DIR. "
                      << "Exited with status " << success;

  const std::string stripped_ramdisk_path = build_dir + "/" + STRIPPED_RD;
  success = execute({"/bin/bash", "-c",
                     HostBinaryPath("mkbootfs") + " " + ramdisk_stage_dir +
                         " > " + stripped_ramdisk_path + CPIO_EXT});
  CHECK(success == 0) << "Unable to run cd or cpio. Exited with status "
                      << success;

  success = execute({"/bin/bash", "-c", HostBinaryPath("lz4") +
                     " -c -l -12 --favor-decSpeed " + stripped_ramdisk_path + CPIO_EXT + " > " +
                     stripped_ramdisk_path});
  CHECK(success == 0) << "Unable to run lz4. Exited with status " << success;

  // Concatenates the stripped ramdisk and input ramdisk and places the result at new_ramdisk_path
  std::ofstream final_rd(new_ramdisk_path, std::ios_base::binary | std::ios_base::trunc);
  std::ifstream ramdisk_a(stripped_ramdisk_path, std::ios_base::binary);
  std::ifstream ramdisk_b(kernel_modules_ramdisk_path, std::ios_base::binary);
  final_rd << ramdisk_a.rdbuf() << ramdisk_b.rdbuf();
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
  Command concat_cmd("/bin/bash");
  concat_cmd.AddParameter("-c");
  concat_cmd.AddParameter("cat " + unpack_dir + "/vendor_ramdisk*");
  auto concat_file =
      SharedFD::Creat(unpack_dir + "/" + CONCATENATED_VENDOR_RAMDISK, 0666);
  if (!concat_file->IsOpen()) {
    LOG(ERROR) << "Unable to create concatenated vendor ramdisk file: "
               << concat_file->StrError();
    return false;
  }
  concat_cmd.RedirectStdIO(Subprocess::StdIOChannel::kStdOut, concat_file);
  success = concat_cmd.Start().Wait();
  if (success != 0) {
    LOG(ERROR) << "Unable to run cat. Exited with status " << success;
    return false;
  }
  return true;
}

}  // namespace

bool RepackBootImage(const std::string& new_kernel_path,
                     const std::string& boot_image_path,
                     const std::string& new_boot_image_path,
                     const std::string& build_dir) {
  if (UnpackBootImage(boot_image_path, build_dir) == false) {
    return false;
  }

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
  int success = repack_cmd.Start().Wait();
  if (success != 0) {
    LOG(ERROR) << "Unable to run mkbootimg. Exited with status " << success;
    return false;
  }

  auto fd = SharedFD::Open(tmp_boot_image_path, O_RDWR);
  auto original_size = FileSize(boot_image_path);
  CHECK(fd->Truncate(original_size) == 0)
    << "`truncate --size=" << original_size << " " << tmp_boot_image_path << "` "
    << "failed: " << fd->StrError();

  return DeleteTmpFileIfNotChanged(tmp_boot_image_path, new_boot_image_path);
}

bool RepackVendorBootImage(const std::string& new_ramdisk,
                           const std::string& vendor_boot_image_path,
                           const std::string& new_vendor_boot_image_path,
                           const std::string& unpack_dir,
                           const std::string& repack_dir,
                           const std::vector<std::string>& bootconfig_args,
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
                          unpack_dir + "/" + CONCATENATED_VENDOR_RAMDISK,
                          ramdisk_path, unpack_dir);
    }
  } else {
    ramdisk_path = unpack_dir + "/" + CONCATENATED_VENDOR_RAMDISK;
  }

  auto bootconfig_fd = SharedFD::Creat(repack_dir + "/bootconfig", 0666);
  if (!bootconfig_fd->IsOpen()) {
    LOG(ERROR) << "Unable to create intermediate bootconfig file: "
               << bootconfig_fd->StrError();
    return false;
  }
  std::string bootconfig = ReadFile(unpack_dir + "/bootconfig");
  bootconfig_fd->Write(bootconfig.c_str(), bootconfig.size());
  LOG(DEBUG) << "Bootconfig parameters from vendor boot image are "
             << ReadFile(repack_dir + "/bootconfig");
  std::string vendor_boot_params = ReadFile(unpack_dir + "/vendor_boot_params");
  auto kernel_cmdline =
      ExtractValue(vendor_boot_params, "vendor command line args: ") +
      (bootconfig_supported
           ? ""
           : " " + android::base::StringReplace(bootconfig, "\n", " ", true) +
                 " " + android::base::Join(bootconfig_args, " "));
  if (!bootconfig_supported) {
    // TODO(b/182417593): Until we pass the module parameters through
    // modules.options, we pass them through bootconfig using
    // 'kernel.<key>=<value>' But if we don't support bootconfig, we need to
    // rename them back to the old cmdline version
    kernel_cmdline = android::base::StringReplace(
        kernel_cmdline, " kernel.", " ", true);
  }
  LOG(DEBUG) << "Cmdline from vendor boot image and config is "
             << kernel_cmdline;

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
    repack_cmd.AddParameter(repack_dir + "/bootconfig");
  }

  int success = repack_cmd.Start().Wait();
  if (success != 0) {
    LOG(ERROR) << "Unable to run mkbootimg. Exited with status " << success;
    return false;
  }

  auto fd = SharedFD::Open(tmp_vendor_boot_image_path, O_RDWR);
  auto original_size = FileSize(vendor_boot_image_path);
  CHECK(fd->Truncate(original_size) == 0)
    << "`truncate --size=" << original_size << " " << tmp_vendor_boot_image_path << "` "
    << "failed: " << fd->StrError();

  return DeleteTmpFileIfNotChanged(tmp_vendor_boot_image_path, new_vendor_boot_image_path);
}

bool RepackVendorBootImageWithEmptyRamdisk(
    const std::string& vendor_boot_image_path,
    const std::string& new_vendor_boot_image_path,
    const std::string& unpack_dir, const std::string& repack_dir,
    const std::vector<std::string>& bootconfig_args,
    bool bootconfig_supported) {
  auto empty_ramdisk_file =
      SharedFD::Creat(unpack_dir + "/empty_ramdisk", 0666);
  return RepackVendorBootImage(
      unpack_dir + "/empty_ramdisk", vendor_boot_image_path,
      new_vendor_boot_image_path, unpack_dir, repack_dir, bootconfig_args,
      bootconfig_supported);
}
} // namespace cuttlefish
