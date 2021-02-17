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

#include "common/libs/utils/files.h"
#include "common/libs/utils/subprocess.h"

const char TMP_EXTENSION[] = ".tmp";
const char CPIO_EXT[] = ".cpio";
const char TMP_RD_DIR[] = "stripped_ramdisk_dir";
const char STRIPPED_RD[] = "stripped_ramdisk";
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

std::string FindCpio() {
  for (const auto& path : {"/usr/bin/cpio", "/bin/cpio"}) {
    if (FileExists(path)) {
      return path;
    }
  }
  LOG(FATAL) << "Could not find a cpio executable.";
  return "";
}

} // namespace

void RepackVendorRamdisk(const std::string& kernel_modules_ramdisk_path,
                         const std::string& original_ramdisk_path,
                         const std::string& new_ramdisk_path,
                         const std::string& build_dir) {
  const auto& cpio_path = FindCpio();
  int success = execute({"/bin/bash", "-c", DefaultHostArtifactsPath("bin/lz4") + " -c -d -l " +
                        original_ramdisk_path + " > " + original_ramdisk_path + CPIO_EXT});
  CHECK(success == 0) << "Unable to run lz4. Exited with status " << success;

  success = mkdir((build_dir + "/" + TMP_RD_DIR).c_str(), S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH);
  CHECK(success == 0) << "Could not mkdir \"" << TMP_RD_DIR << "\", error was " << strerror(errno);

  success = execute({"/bin/bash", "-c",
                     "(cd " + build_dir + "/" + TMP_RD_DIR + " && (while " +
                         cpio_path + " -id ; do :; done) < " +
                         original_ramdisk_path + CPIO_EXT + ")"});
  CHECK(success == 0) << "Unable to run cd or cpio. Exited with status " << success;

  success = execute({"/bin/bash", "-c", "rm -rf " + build_dir + "/" + TMP_RD_DIR + "/lib/modules"});
  CHECK(success == 0) << "Could not rmdir \"lib/modules\" in TMP_RD_DIR. Exited with status "
                  << success;

  const std::string stripped_ramdisk_path = build_dir + "/" + STRIPPED_RD;
  success = execute({"/bin/bash", "-c",
                     "(cd " + build_dir + "/" + TMP_RD_DIR + " && find . | " +
                         cpio_path + " -H newc -o --quiet > " +
                         stripped_ramdisk_path + CPIO_EXT + ")"});
  CHECK(success == 0) << "Unable to run cd or cpio. Exited with status " << success;

  success = execute({"/bin/bash", "-c", DefaultHostArtifactsPath("bin/lz4") +
                     " -c -l -12 --favor-decSpeed " + stripped_ramdisk_path + CPIO_EXT + " > " +
                     stripped_ramdisk_path});
  CHECK(success == 0) << "Unable to run lz4. Exited with status " << success;

  // Concatenates the stripped ramdisk and input ramdisk and places the result at new_ramdisk_path
  std::ofstream final_rd(new_ramdisk_path, std::ios_base::binary | std::ios_base::trunc);
  std::ifstream ramdisk_a(stripped_ramdisk_path, std::ios_base::binary);
  std::ifstream ramdisk_b(kernel_modules_ramdisk_path, std::ios_base::binary);
  final_rd << ramdisk_a.rdbuf() << ramdisk_b.rdbuf();
}

bool RepackBootImage(const std::string& new_kernel_path,
                     const std::string& boot_image_path,
                     const std::string& new_boot_image_path,
                     const std::string& build_dir) {
  auto tmp_boot_image_path = new_boot_image_path + TMP_EXTENSION;
  auto unpack_path = DefaultHostArtifactsPath("bin/unpack_bootimg");
  Command unpack_cmd(unpack_path);
  unpack_cmd.AddParameter("--boot_img");
  unpack_cmd.AddParameter(boot_image_path);
  unpack_cmd.AddParameter("--out");
  unpack_cmd.AddParameter(build_dir);
  int success = unpack_cmd.Start().Wait();
  if (success != 0) {
    LOG(ERROR) << "Unable to run unpack_bootimg. Exited with status " << success;
    return false;
  }

  auto repack_path = DefaultHostArtifactsPath("bin/mkbootimg");
  Command repack_cmd(repack_path);
  repack_cmd.AddParameter("--kernel");
  repack_cmd.AddParameter(new_kernel_path);
  repack_cmd.AddParameter("--ramdisk");
  repack_cmd.AddParameter(build_dir + "/ramdisk");
  repack_cmd.AddParameter("--header_version");
  repack_cmd.AddParameter("3");
  repack_cmd.AddParameter("-o");
  repack_cmd.AddParameter(tmp_boot_image_path);
  success = repack_cmd.Start().Wait();
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

bool RepackVendorBootImage(const std::string& kernel_modules_ramdisk_path,
                           const std::string& vendor_boot_image_path,
                           const std::string& new_vendor_boot_image_path,
                           const std::string& build_dir) {
  auto tmp_vendor_boot_image_path = new_vendor_boot_image_path + TMP_EXTENSION;
  auto unpack_path = DefaultHostArtifactsPath("bin/unpack_bootimg");
  Command unpack_cmd(unpack_path);
  unpack_cmd.AddParameter("--boot_img");
  unpack_cmd.AddParameter(vendor_boot_image_path);
  unpack_cmd.AddParameter("--out");
  unpack_cmd.AddParameter(build_dir);
  auto output_file = SharedFD::Creat(build_dir + "/vendor_boot_params", 0666);
  if (!output_file->IsOpen()) {
    LOG(ERROR) << "Unable to create intermediate params file: "
               << output_file->StrError();
    return false;
  }
  unpack_cmd.RedirectStdIO(Subprocess::StdIOChannel::kStdOut, output_file);
  int success = unpack_cmd.Start().Wait();
  if (success != 0) {
    LOG(ERROR) << "Unable to run unpack_bootimg. Exited with status " << success;
    return false;
  }

  // TODO(b/173134558)
  // The vendor boot generation below isn't deterministic. i.e. running the same vendor boot
  // repack function twice with the same inputs will produce two differing vendor boot images.
  // This is because the vendor boot ramdisk contains a few symlinks. These symlinks affect the
  // ramdisk regeneration process and cause differing outputs each time (I still haven't figured
  // out why).
  std::string new_ramdisk_path = build_dir + "/vendor_ramdisk_repacked";
  RepackVendorRamdisk(kernel_modules_ramdisk_path, build_dir + "/vendor_ramdisk",
                      new_ramdisk_path, build_dir);

  std::string vendor_boot_params = ReadFile(build_dir + "/vendor_boot_params");
  auto kernel_cmdline = "\"" + ExtractValue(vendor_boot_params, "vendor command line args: ") + "\"";
  LOG(DEBUG) << "Cmdline from vendor boot image is " << kernel_cmdline;

  auto repack_path = DefaultHostArtifactsPath("bin/mkbootimg");
  Command repack_cmd(repack_path);
  repack_cmd.AddParameter("--vendor_ramdisk");
  repack_cmd.AddParameter(new_ramdisk_path);
  repack_cmd.AddParameter("--header_version");
  repack_cmd.AddParameter("3");
  repack_cmd.AddParameter("--cmdline");
  repack_cmd.AddParameter(kernel_cmdline);
  repack_cmd.AddParameter("--vendor_boot");
  repack_cmd.AddParameter(tmp_vendor_boot_image_path);
  repack_cmd.AddParameter("--dtb");
  repack_cmd.AddParameter(build_dir + "/dtb");
  success = repack_cmd.Start().Wait();
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
    const std::string& build_dir) {
  auto empty_ramdisk_file = SharedFD::Creat(build_dir + "/empty_ramdisk", 0666);
  return RepackVendorBootImage(build_dir + "/empty_ramdisk", vendor_boot_image_path,
      new_vendor_boot_image_path, build_dir);
}
} // namespace cuttlefish
