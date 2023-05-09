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
#include <android-base/stringprintf.h>
#include <android-base/strings.h>

#include <fcntl.h>

#include <fcntl.h>
#include <algorithm>
#include <iterator>
#include <map>
#include <queue>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include <fmt/format.h>

#include "common/libs/utils/files.h"
#include "common/libs/utils/subprocess.h"
#include "fmt/core.h"
#include "host/commands/assemble_cvd/boot_image_utils.h"
#include "host/commands/assemble_cvd/kernel_module_parser.h"
#include "host/commands/assemble_cvd/ramdisk_modules.h"
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

std::vector<std::string> GetRamdiskModules(
    const std::vector<std::string>& all_modules) {
  static const auto ramdisk_modules_allow_list =
      std::set<std::string>(RAMDISK_MODULES.begin(), RAMDISK_MODULES.end());
  std::vector<std::string> ramdisk_modules;
  for (const auto& mod_path : all_modules) {
    if (mod_path.empty()) {
      continue;
    }
    const auto mod_name = cpp_basename(mod_path);
    if (ramdisk_modules_allow_list.count(mod_name) != 0) {
      ramdisk_modules.emplace_back(mod_path);
    }
  }
  return ramdisk_modules;
}

// Filter the dependency map |deps| to only contain nodes in |allow_list|
std::map<std::string, std::vector<std::string>> FilterDependencies(
    const std::map<std::string, std::vector<std::string>>& deps,
    const std::set<std::string>& allow_list) {
  std::map<std::string, std::vector<std::string>> new_deps;
  for (const auto& [mod_name, children] : deps) {
    if (!allow_list.count(mod_name)) {
      continue;
    }
    new_deps[mod_name].clear();
    for (const auto& child : children) {
      if (!allow_list.count(child)) {
        continue;
      }
      new_deps[mod_name].emplace_back(child);
    }
  }
  return new_deps;
}

std::map<std::string, std::vector<std::string>> FilterOutDependencies(
    const std::map<std::string, std::vector<std::string>>& deps,
    const std::set<std::string>& block_list) {
  std::map<std::string, std::vector<std::string>> new_deps;
  for (const auto& [mod_name, children] : deps) {
    if (block_list.count(mod_name)) {
      continue;
    }
    new_deps[mod_name].clear();
    for (const auto& child : children) {
      if (block_list.count(child)) {
        continue;
      }
      new_deps[mod_name].emplace_back(child);
    }
  }
  return new_deps;
}

// Update dependency map by prepending '/system/lib/modules' to modules which
// have been relocated to system_dlkm partition
std::map<std::string, std::vector<std::string>> UpdateGKIModulePaths(
    const std::map<std::string, std::vector<std::string>>& deps,
    const std::set<std::string>& gki_modules) {
  std::map<std::string, std::vector<std::string>> new_deps;
  auto&& GetNewModName = [gki_modules](auto&& mod_name) {
    if (gki_modules.count(mod_name)) {
      return "/system/lib/modules/" + mod_name;
    } else {
      return mod_name;
    }
  };
  for (const auto& [old_mod_name, children] : deps) {
    const auto mod_name = GetNewModName(old_mod_name);
    new_deps[mod_name].clear();

    for (const auto& child : children) {
      new_deps[mod_name].emplace_back(GetNewModName(child));
    }
  }
  return new_deps;
}

// Write dependency map to modules.dep file
bool WriteDepsToFile(
    const std::map<std::string, std::vector<std::string>>& deps,
    const std::string& output_path) {
  std::stringstream ss;
  for (const auto& [key, val] : deps) {
    ss << key << ":";
    for (const auto& dep : val) {
      ss << " " << dep;
    }
    ss << "\n";
  }
  if (!android::base::WriteStringToFile(ss.str(), output_path)) {
    PLOG(ERROR) << "Failed to write modules.dep to " << output_path;
    return false;
  }
  return true;
}

// Parse modules.dep into an in-memory data structure, key is path to a kernel
// module, value is all dependency modules
std::map<std::string, std::vector<std::string>> LoadModuleDeps(
    const std::string& filename) {
  std::map<std::string, std::vector<std::string>> dependency_map;
  const auto dep_str = android::base::Trim(ReadFile(filename));
  const auto dep_lines = android::base::Split(dep_str, "\n");
  for (const auto& line : dep_lines) {
    const auto mod_name = line.substr(0, line.find(":"));
    auto deps = android::base::Tokenize(line.substr(mod_name.size() + 1), " ");
    dependency_map[mod_name] = std::move(deps);
  }

  return dependency_map;
}

// Recursively compute all modules which |start_nodes| depend on
template <typename StringContainer>
std::set<std::string> ComputeTransitiveClosure(
    const StringContainer& start_nodes,
    const std::map<std::string, std::vector<std::string>>& dependencies) {
  std::deque<std::string> queue(start_nodes.begin(), start_nodes.end());
  std::set<std::string> visited;
  while (!queue.empty()) {
    const auto cur = queue.front();
    queue.pop_front();
    if (visited.find(cur) != visited.end()) {
      continue;
    }
    visited.insert(cur);
    const auto it = dependencies.find(cur);
    if (it == dependencies.end()) {
      continue;
    }
    for (const auto& dep : it->second) {
      queue.emplace_back(dep);
    }
  }
  return visited;
}

// Generate a file_context.bin file which can be used by selinux tools to assign
// selinux labels to files
bool GenerateFileContexts(const char* output_path,
                          const std::string& mount_point,
                          std::string_view file_label) {
  const auto file_contexts_txt = std::string(output_path) + ".txt";
  android::base::unique_fd fd(open(file_contexts_txt.c_str(),
                                   O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC,
                                   0644));
  if (!fd.ok()) {
    PLOG(ERROR) << "Failed to open " << output_path;
    return false;
  }

  if (!android::base::WriteStringToFd(
          fmt::format("{}(/.*)?         u:object_r:{}:s0\n", mount_point,
                      file_label),
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
bool BuildDlkmImage(const std::string& src_dir, const bool is_erofs,
                    const std::string& partition_name,
                    const std::string& output_image) {
  if (is_erofs) {
    LOG(ERROR)
        << "Building DLKM image in EROFS format is currently not supported!";
    return false;
  }
  const auto mount_point = "/" + partition_name;
  const auto fs_config = output_image + ".fs_config";
  if (!WriteFsConfig(fs_config.c_str(), src_dir, mount_point)) {
    return false;
  }
  const auto file_contexts_bin = output_image + ".file_contexts";
  if (partition_name == "system_dlkm") {
    if (!GenerateFileContexts(file_contexts_bin.c_str(), mount_point,
                              "system_dlkm_file")) {
      return false;
    }
  } else {
    if (!GenerateFileContexts(file_contexts_bin.c_str(), mount_point,
                              "vendor_file")) {
      return false;
    }
  }

  // We are using directory size as an estimate of final image size. To avoid
  // any rounding errors, add 16M of head room.
  const auto fs_size = RoundUp(GetDiskUsage(src_dir) + 16 * 1024 * 1024, 4096);
  LOG(INFO) << mount_point << " src dir " << src_dir << " has size "
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
  mkfs_cmd.AddParameter(mount_point);
  mkfs_cmd.AddParameter(std::to_string(fs_size));
  mkfs_cmd.AddParameter(file_contexts_bin);

  int exit_code = mkfs_cmd.Start().Wait();
  if (exit_code != 0) {
    LOG(ERROR) << "Failed to build vendor_dlkm ext4 image";
    return false;
  }
  return AddVbmetaFooter(output_image, partition_name);
}

bool BuildVendorDLKM(const std::string& src_dir, const bool is_erofs,
                     const std::string& output_image) {
  return BuildDlkmImage(src_dir, is_erofs, "vendor_dlkm", output_image);
}

bool BuildSystemDLKM(const std::string& src_dir, const bool is_erofs,
                     const std::string& output_image) {
  return BuildDlkmImage(src_dir, is_erofs, "system_dlkm", output_image);
}

bool RepackSuperWithPartition(const std::string& superimg_path,
                              const std::string& image_path,
                              const std::string& partition_name) {
  Command lpadd(HostBinaryPath("lpadd"));
  lpadd.AddParameter("--replace");
  lpadd.AddParameter(superimg_path);
  lpadd.AddParameter(partition_name + "_a");
  lpadd.AddParameter("google_vendor_dynamic_partitions_a");
  lpadd.AddParameter(image_path);
  const auto exit_code = lpadd.Start().Wait();
  return exit_code == 0;
}

bool BuildVbmetaImage(const std::string& image_path,
                      const std::string& vbmeta_path) {
  CHECK(!image_path.empty());
  CHECK(FileExists(image_path));
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
  vbmeta_cmd.AddParameter(image_path);
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

bool SplitRamdiskModules(const std::string& ramdisk_path,
                         const std::string& ramdisk_stage_dir,
                         const std::string& vendor_dlkm_build_dir,
                         const std::string& system_dlkm_build_dir) {
  const auto vendor_modules_dir = vendor_dlkm_build_dir + "/lib/modules";
  const auto system_modules_dir = system_dlkm_build_dir + "/lib/modules";
  auto ret = EnsureDirectoryExists(vendor_modules_dir);
  CHECK(ret.ok()) << ret.error().Message();
  ret = EnsureDirectoryExists(system_modules_dir);
  UnpackRamdisk(ramdisk_path, ramdisk_stage_dir);
  const auto module_load_file =
      android::base::Trim(FindFile(ramdisk_stage_dir.c_str(), "modules.load"));
  if (module_load_file.empty()) {
    LOG(ERROR) << "Failed to find modules.dep file in input ramdisk "
               << ramdisk_path;
    return false;
  }
  LOG(INFO) << "modules.load location " << module_load_file;
  const auto module_list =
      android::base::Tokenize(ReadFile(module_load_file), "\n");
  const auto module_base_dir = cpp_dirname(module_load_file);
  const auto deps = LoadModuleDeps(module_base_dir + "/modules.dep");
  const auto ramdisk_modules =
      ComputeTransitiveClosure(GetRamdiskModules(module_list), deps);
  std::set<std::string> vendor_dlkm_modules;
  std::set<std::string> system_dlkm_modules;

  // Move non-ramdisk modules to vendor_dlkm
  for (const auto& module_path : module_list) {
    if (ramdisk_modules.count(module_path)) {
      continue;
    }

    const auto module_location =
        fmt::format("{}/{}", module_base_dir, module_path);
    if (IsKernelModuleSigned(module_location)) {
      const auto system_dlkm_module_location =
          fmt::format("{}/{}", system_modules_dir, module_path);
      EnsureDirectoryExists(cpp_dirname(system_dlkm_module_location));
      RenameFile(module_location, system_dlkm_module_location);
      system_dlkm_modules.emplace(module_path);
    } else {
      const auto vendor_dlkm_module_location =
          fmt::format("{}/{}", vendor_modules_dir, module_path);
      EnsureDirectoryExists(cpp_dirname(vendor_dlkm_module_location));
      RenameFile(module_location, vendor_dlkm_module_location);
      vendor_dlkm_modules.emplace(module_path);
    }
  }
  for (const auto& gki_module : system_dlkm_modules) {
    for (const auto& dep : deps.at(gki_module)) {
      if (vendor_dlkm_modules.count(dep)) {
        LOG(ERROR) << "GKI module " << gki_module
                   << " depends on vendor_dlkm module " << dep;
        return false;
      }
    }
  }
  LOG(INFO) << "There are " << ramdisk_modules.size() << " ramdisk modules, "
            << vendor_dlkm_modules.size() << " vendor_dlkm modules, "
            << system_dlkm_modules.size() << " system_dlkm modules.";

  // Write updated modules.dep and modules.load files
  CHECK(WriteDepsToFile(FilterDependencies(deps, ramdisk_modules),
                        module_base_dir + "/modules.dep"));
  CHECK(WriteLinesToFile(ramdisk_modules, module_load_file.c_str()));

  CHECK(WriteDepsToFile(
      UpdateGKIModulePaths(FilterOutDependencies(deps, ramdisk_modules),
                           system_dlkm_modules),
      vendor_modules_dir + "/modules.dep"));
  CHECK(WriteLinesToFile(vendor_dlkm_modules,
                         (vendor_modules_dir + "/modules.load").c_str()));

  CHECK(WriteDepsToFile(FilterDependencies(deps, system_dlkm_modules),
                        system_modules_dir + "/modules.dep"));
  CHECK(WriteLinesToFile(system_dlkm_modules,
                         (system_modules_dir + "/modules.load").c_str()));
  PackRamdisk(ramdisk_stage_dir, ramdisk_path);
  return true;
}

bool FileEquals(const std::string& file1, const std::string& file2) {
  if (FileSize(file1) != FileSize(file2)) {
    return false;
  }
  std::array<uint8_t, 1024 * 16> buf1{};
  std::array<uint8_t, 1024 * 16> buf2{};
  auto fd1 = SharedFD::Open(file1, O_RDONLY);
  auto fd2 = SharedFD::Open(file2, O_RDONLY);
  auto bytes_remain = FileSize(file1);
  while (bytes_remain > 0) {
    const auto bytes_to_read = std::min<size_t>(bytes_remain, buf1.size());
    if (fd1->Read(buf1.data(), bytes_to_read) != bytes_to_read) {
      LOG(ERROR) << "Failed to read from " << file1;
      return false;
    }
    if (!fd2->Read(buf2.data(), bytes_to_read)) {
      LOG(ERROR) << "Failed to read from " << file2;
      return false;
    }
    if (memcmp(buf1.data(), buf2.data(), bytes_to_read)) {
      return false;
    }
  }
  return true;
}

bool MoveIfChanged(const std::string& src, const std::string& dst) {
  if (FileEquals(src, dst)) {
    return false;
  }
  const auto ret = RenameFile(src, dst);
  if (!ret.ok()) {
    LOG(ERROR) << ret.error().Message();
    return false;
  }
  return true;
}

}  // namespace cuttlefish
