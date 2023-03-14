//
// Copyright (C) 2023 The Android Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <android-base/file.h>
#include <android-base/logging.h>
#include <android-base/strings.h>

#include <fcntl.h>

#include <map>
#include <queue>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "common/libs/utils/files.h"
#include "common/libs/utils/subprocess.h"
#include "host/commands/assemble_cvd/boot_image_utils.h"
#include "host/libs/config/cuttlefish_config.h"

namespace cuttlefish {

namespace {

constexpr size_t RoundDown(size_t a, size_t divisor) {
  return a / divisor * divisor;
}
constexpr size_t RoundUp(size_t a, size_t divisor) {
  return RoundDown(a + divisor, divisor);
}

template <typename Container>
bool WriteLinesToFile(const Container& lines, const char* path) {
  android::base::unique_fd fd(
      open(path, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0640));
  if (!fd.ok()) {
    PLOG(ERROR) << "Failed to open " << path;
    return false;
  }
  for (const auto& line : lines) {
    if (!android::base::WriteFully(fd, line.data(), line.size())) {
      PLOG(ERROR) << "Failed to write to " << path;
      return false;
    }
    const char c = '\n';
    if (write(fd.get(), &c, 1) != 1) {
      PLOG(ERROR) << "Failed to write to " << path;
      return false;
    }
  }
  return true;
}

// Recursively enumerate files in |dir|, and invoke the callback function with
// path to each file/directory.
Result<void> WalkDirectory(
    const std::string& dir,
    const std::function<bool(const std::string&)>& callback) {
  const auto files = CF_EXPECT(DirectoryContents(dir));
  for (const auto& filename : files) {
    if (filename == "." || filename == "..") {
      continue;
    }
    auto file_path = dir + "/";
    file_path.append(filename);
    callback(file_path);
    if (DirectoryExists(file_path)) {
      WalkDirectory(file_path, callback);
    }
  }
  return {};
}

// Generate a filesystem_config.txt for all files in |fs_root|
bool WriteFsConfig(const char* output_path, const std::string& fs_root,
                   const std::string& mount_point) {
  android::base::unique_fd fd(
      open(output_path, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644));
  if (!fd.ok()) {
    PLOG(ERROR) << "Failed to open " << output_path;
    return false;
  }
  if (!android::base::WriteStringToFd(
          " 0 0 755 selabel=u:object_r:rootfs:s0 capabilities=0x0\n", fd)) {
    PLOG(ERROR) << "Failed to write to " << output_path;
    return false;
  }
  WalkDirectory(fs_root, [&fd, &output_path, &mount_point,
                          &fs_root](const std::string& file_path) {
    const auto filename = file_path.substr(
        fs_root.back() == '/' ? fs_root.size() : fs_root.size() + 1);
    std::string fs_context = " 0 0 644 capabilities=0x0\n";
    if (DirectoryExists(file_path)) {
      fs_context = " 0 0 755 capabilities=0x0\n";
    }
    if (!android::base::WriteStringToFd(
            mount_point + "/" + filename + fs_context, fd)) {
      PLOG(ERROR) << "Failed to write to " << output_path;
      return false;
    }
    return true;
  });
  return true;
}

bool GenerateFileContexts(const char* output_path,
                          const std::string& mount_point) {
  const auto file_contexts_txt = std::string(output_path) + ".txt";
  android::base::unique_fd fd(open(file_contexts_txt.c_str(),
                                   O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC,
                                   0644));
  if (!fd.ok()) {
    PLOG(ERROR) << "Failed to open " << output_path;
    return false;
  }
  if (!android::base::WriteStringToFd(mount_point +
                                          "(/.*)?       "
                                          "  u:object_r:vendor_file:s0\n",
                                      fd)) {
    return false;
  }
  if (!android::base::WriteStringToFd(
          mount_point + "/etc(/.*)?       "
                        "  u:object_r:vendor_configs_file:s0\n",
          fd)) {
    return false;
  }
  Command cmd(HostBinaryPath("sefcontext_compile"));
  cmd.AddParameter("-o");
  cmd.AddParameter(output_path);
  cmd.AddParameter(file_contexts_txt);
  const auto exit_code = cmd.Start().Wait();
  return exit_code == 0;
}

bool AddVbmetaFooter(const std::string& output_image,
                     const std::string& partition_name) {
  auto avbtool_path = HostBinaryPath("avbtool");
  Command avb_cmd(avbtool_path);
  // Add host binary path to PATH, so that avbtool can locate host util
  // binaries such as 'fec'
  auto PATH =
      StringFromEnv("PATH", "") + ":" + cpp_dirname(avb_cmd.Executable());
  // Must unset an existing environment variable in order to modify it
  avb_cmd.UnsetFromEnvironment("PATH");
  avb_cmd.AddEnvironmentVariable("PATH", PATH);

  avb_cmd.AddParameter("add_hashtree_footer");
  // Arbitrary salt to keep output consistent
  avb_cmd.AddParameter("--salt");
  avb_cmd.AddParameter("62BBAAA0", "E4BD99E783AC");
  avb_cmd.AddParameter("--image");
  avb_cmd.AddParameter(output_image);
  avb_cmd.AddParameter("--partition_name");
  avb_cmd.AddParameter(partition_name);

  auto exit_code = avb_cmd.Start().Wait();
  if (exit_code != 0) {
    LOG(ERROR) << "Failed to add avb footer to image " << output_image;
    return false;
  }

  return true;
}

}  // namespace

// Steps for building a vendor_dlkm.img:
// 1. Generate filesystem_config.txt , which contains standard linux file
// permissions, we use 0755 for directories, and 0644 for all files
// 2. Write file_contexts, which contains all selinux labels
// 3. Call  sefcontext_compile to compile file_contexts
// 4. call mkuserimg_mke2fs to build an image, using filesystem_config and
// file_contexts previously generated
// 5. call avbtool to add hashtree footer, so that init/bootloader can verify
// AVB chain
bool BuildVendorDLKM(const std::string& src_dir, const bool is_erofs,
                     const std::string& output_image) {
  if (is_erofs) {
    LOG(ERROR)
        << "Building vendor_dlkm in EROFS format is currently not supported!";
    return false;
  }
  const auto fs_config = output_image + ".fs_config";
  if (!WriteFsConfig(fs_config.c_str(), src_dir, "/vendor_dlkm")) {
    return false;
  }
  const auto file_contexts_bin = output_image + ".file_contexts";
  if (!GenerateFileContexts(file_contexts_bin.c_str(), "/vendor_dlkm")) {
    return false;
  }

  // We are using directory size as an estimate of final image size. To avoid
  // any rounding errors, add 16M of head room.
  const auto fs_size = RoundUp(GetDiskUsage(src_dir) + 16 * 1024 * 1024, 4096);
  LOG(INFO) << "vendor_dlkm src dir " << src_dir << " has size "
            << fs_size / 1024 << " KB";
  const auto mkfs = HostBinaryPath("mkuserimg_mke2fs");
  Command mkfs_cmd(mkfs);
  // Arbitrary UUID/seed, just to keep output consistent between runs
  mkfs_cmd.AddParameter("--mke2fs_uuid");
  mkfs_cmd.AddParameter("cb09b942-ed4e-46a1-81dd-7d535bf6c4b1");
  mkfs_cmd.AddParameter("--mke2fs_hash_seed");
  mkfs_cmd.AddParameter("765d8aba-d93f-465a-9fcf-14bb794eb7f4");
  // Arbitrary date, just to keep output consistent
  mkfs_cmd.AddParameter("-T");
  mkfs_cmd.AddParameter("900979200000");

  // selinux permission to keep selinux happy
  mkfs_cmd.AddParameter("--fs_config");
  mkfs_cmd.AddParameter(fs_config);

  mkfs_cmd.AddParameter(src_dir);
  mkfs_cmd.AddParameter(output_image);
  mkfs_cmd.AddParameter("ext4");
  mkfs_cmd.AddParameter("/vendor_dlkm");
  mkfs_cmd.AddParameter(std::to_string(fs_size));
  mkfs_cmd.AddParameter(file_contexts_bin);

  int exit_code = mkfs_cmd.Start().Wait();
  if (exit_code != 0) {
    LOG(ERROR) << "Failed to build vendor_dlkm ext4 image";
    return false;
  }
  return AddVbmetaFooter(output_image, "vendor_dlkm");
}

bool RepackSuperWithVendorDLKM(const std::string& superimg_path,
                               const std::string& vendor_dlkm_path) {
  Command lpadd(HostBinaryPath("lpadd"));
  lpadd.AddParameter("--replace");
  lpadd.AddParameter(superimg_path);
  lpadd.AddParameter("vendor_dlkm_a");
  lpadd.AddParameter("google_vendor_dynamic_partitions_a");
  lpadd.AddParameter(vendor_dlkm_path);
  const auto exit_code = lpadd.Start().Wait();
  return exit_code == 0;
}

bool RebuildVbmetaVendor(const std::string& vendor_dlkm_img,
                         const std::string& vbmeta_path) {
  auto avbtool_path = HostBinaryPath("avbtool");
  Command vbmeta_cmd(avbtool_path);
  vbmeta_cmd.AddParameter("make_vbmeta_image");
  vbmeta_cmd.AddParameter("--output");
  vbmeta_cmd.AddParameter(vbmeta_path);
  vbmeta_cmd.AddParameter("--algorithm");
  vbmeta_cmd.AddParameter("SHA256_RSA4096");
  vbmeta_cmd.AddParameter("--key");
  vbmeta_cmd.AddParameter(DefaultHostArtifactsPath("etc/cvd_avb_testkey.pem"));

  vbmeta_cmd.AddParameter("--include_descriptors_from_image");
  vbmeta_cmd.AddParameter(vendor_dlkm_img);
  vbmeta_cmd.AddParameter("--padding_size");
  vbmeta_cmd.AddParameter("4096");

  bool success = vbmeta_cmd.Start().Wait();
  if (success != 0) {
    LOG(ERROR) << "Unable to create vbmeta. Exited with status " << success;
    return false;
  }

  const auto vbmeta_size = FileSize(vbmeta_path);
  if (vbmeta_size > VBMETA_MAX_SIZE) {
    LOG(ERROR) << "Generated vbmeta - " << vbmeta_path
               << " is larger than the expected " << VBMETA_MAX_SIZE
               << ". Stopping.";
    return false;
  }
  if (vbmeta_size != VBMETA_MAX_SIZE) {
    auto fd = SharedFD::Open(vbmeta_path, O_RDWR | O_CLOEXEC);
    if (!fd->IsOpen() || fd->Truncate(VBMETA_MAX_SIZE) != 0) {
      LOG(ERROR) << "`truncate --size=" << VBMETA_MAX_SIZE << " " << vbmeta_path
                 << "` failed: " << fd->StrError();
      return false;
    }
  }
  return true;
}

}  // namespace cuttlefish
