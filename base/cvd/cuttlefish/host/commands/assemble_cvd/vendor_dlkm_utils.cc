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

#include "cuttlefish/host/commands/assemble_cvd/vendor_dlkm_utils.h"

#include <errno.h>
#include <fcntl.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <deque>
#include <map>
#include <memory>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/log/log.h"
#include "absl/strings/ascii.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_split.h"
#include "android-base/file.h"
#include "android-base/unique_fd.h"
#include "fmt/format.h"
#include "liblp/builder.h"
#include "liblp/liblp.h"
#include "liblp/metadata_format.h"
#include "liblp/partition_opener.h"
#include "sparse/sparse.h"

#include "cuttlefish/common/libs/fs/fd.h"
#include "cuttlefish/common/libs/fs/shared_buf.h"
#include "cuttlefish/common/libs/fs/shared_fd.h"
#include "cuttlefish/common/libs/utils/contains.h"
#include "cuttlefish/common/libs/utils/disk_usage.h"
#include "cuttlefish/common/libs/utils/environment.h"
#include "cuttlefish/common/libs/utils/files.h"
#include "cuttlefish/files/directory_exists.h"
#include "cuttlefish/files/file_exists.h"
#include "cuttlefish/files/recursively_remove_directory.h"
#include "cuttlefish/host/commands/assemble_cvd/boot_image_utils.h"
#include "cuttlefish/host/commands/assemble_cvd/kernel_module_parser.h"
#include "cuttlefish/host/libs/avb/avb.h"
#include "cuttlefish/host/libs/config/config_utils.h"
#include "cuttlefish/host/libs/config/known_paths.h"
#include "cuttlefish/host/libs/image_aggregator/sparse_image.h"
#include "cuttlefish/io/write_exact.h"
#include "cuttlefish/posix/remove.h"
#include "cuttlefish/posix/rename.h"
#include "cuttlefish/posix/strerror.h"
#include "cuttlefish/process/command.h"
#include "cuttlefish/process/execute.h"
#include "cuttlefish/result/result.h"

namespace cuttlefish {

namespace {

constexpr size_t RoundDown(size_t a, size_t divisor) {
  return a / divisor * divisor;
}

constexpr size_t RoundUp(size_t a, size_t divisor) {
  return RoundDown(a + divisor, divisor);
}

template <typename Container>
bool WriteLinesToFile(const Container& lines, const std::string& path) {
  SharedFD fd = Fd::Open(path, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0640)
                    .value_or(Fd());
  if (!fd->IsOpen()) {
    PLOG(ERROR) << "Failed to open " << path;
    return false;
  }
  for (const auto& line : lines) {
    if (WriteAll(fd, line) != line.size()) {
      PLOG(ERROR) << "Failed to write to " << path;
      return false;
    }
    const char c = '\n';
    if (fd->Write(&c, 1) != 1) {
      PLOG(ERROR) << "Failed to write to " << path;
      return false;
    }
  }
  return true;
}

// Generate a filesystem_config.txt for all files in |fs_root|
Result<void> WriteFsConfig(const std::string& output_path,
                           const std::string& fs_root,
                           const std::string& mount_point) {
  Fd fd = CF_EXPECT(
      Fd::Open(output_path, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644));
  static constexpr std::string_view kBeginning =
      " 0 0 755 selabel=u:object_r:rootfs:s0 capabilities=0x0\n";
  CF_EXPECTF(WriteExact(fd, kBeginning), "Failed to write to '{}'",
             output_path);
  Result<void> res = WalkDirectory(
      fs_root,
      [&fd, &output_path, &mount_point,
       &fs_root](const std::string& file_path) -> Result<void> {
        const std::string filename = file_path.substr(
            fs_root.back() == '/' ? fs_root.size() : fs_root.size() + 1);
        const std::string fs_context = DirectoryExists(file_path)
                                           ? " 0 0 755 capabilities=0x0\n"
                                           : " 0 0 644 capabilities=0x0\n";
        std::string to_write = mount_point + "/" + filename + fs_context;
        CF_EXPECTF(WriteExact(fd, to_write), "Failed to write to '{}'",
                   output_path);
        return {};
      });
  CF_EXPECT(std::move(res));
  return {};
}

std::vector<std::string> GetRamdiskModules(
    const std::set<std::string>& all_modules) {
  static constexpr auto kRamdiskModules = {
      "failover.ko",
      "nd_virtio.ko",
      "net_failover.ko",
      "virtio_blk.ko",
      "virtio_console.ko",
      "virtio_dma_buf.ko",
      "virtio-gpu.ko",
      "virtio_input.ko",
      "virtio_mmio.ko",
      "virtio_net.ko",
      "virtio_pci.ko",
      "virtio_pci_legacy_dev.ko",
      "virtio_pci_modern_dev.ko",
      "virtio-rng.ko",
      "vmw_vsock_virtio_transport.ko",
      "vmw_vsock_virtio_transport_common.ko",
      "vsock.ko",
      "pkvm_iommu_temp.ko",
      // TODO(b/176860479) once virt_wifi is deprecated fully,
      // these following modules can be loaded in second stage init
      "libarc4.ko",
      "rfkill.ko",
      "cfg80211.ko",
      "mac80211.ko",
      "mac80211_hwsim.ko",
  };
  std::vector<std::string> ramdisk_modules;
  for (const auto& mod_path : all_modules) {
    if (Contains(kRamdiskModules, android::base::Basename(mod_path))) {
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
  const std::string dep_str(absl::StripAsciiWhitespace(ReadFile(filename)));
  const std::vector<std::string_view> dep_lines = absl::StrSplit(dep_str, "\n");
  for (const auto& line : dep_lines) {
    const auto mod_name = line.substr(0, line.find(":"));
    std::vector<std::string> deps = absl::StrSplit(
        line.substr(mod_name.size() + 1), ' ', absl::SkipEmpty());
    dependency_map.emplace(mod_name, std::move(deps));
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
Result<void> GenerateFileContexts(const std::string& output_path,
                                  std::string_view mount_point,
                                  std::string_view file_label) {
  const std::string file_contexts_txt = output_path + ".txt";
  Fd fd = CF_EXPECT(Fd::Open(file_contexts_txt,
                             O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644));

  std::string line = fmt::format("{}(/.*)?         u:object_r:{}:s0\n",
                                 mount_point, file_label);
  CF_EXPECT(WriteExact(fd, line));

  int exit_code = Execute({
      HostBinaryPath("sefcontext_compile"),
      "-o",
      output_path,
      file_contexts_txt,
  });

  CF_EXPECT_EQ(exit_code, 0);

  return {};
}

Result<void> AddVbmetaFooter(const std::string& output_image,
                             const std::string& partition_name) {
  // TODO(b/335742241): update to use Avb
  std::string avbtool_path = AvbToolBinary();
  // Add host binary path to PATH, so that avbtool can locate host util binaries
  // such as 'fec'
  std::string env_path =
      StringFromEnv("PATH", "") + ":" + android::base::Dirname(avbtool_path);
  Command avb_cmd =
      Command(AvbToolBinary())
          // Must unset an existing environment variable in order to modify it
          .UnsetFromEnvironment("PATH")
          .AddEnvironmentVariable("PATH", env_path)
          .AddParameter("add_hashtree_footer")
          // Arbitrary salt to keep output consistent
          .AddParameter("--salt")
          .AddParameter("62BBAAA0", "E4BD99E783AC")
          .AddParameter("--hash_algorithm")
          .AddParameter("sha256")
          .AddParameter("--image")
          .AddParameter(output_image)
          .AddParameter("--partition_name")
          .AddParameter(partition_name);

  CF_EXPECT_EQ(avb_cmd.Start().Wait(), 0,
               "Failed to add avb footer to image " << output_image);

  return {};
}

class ScopedTempDir {
 public:
  static Result<ScopedTempDir> Create(std::string_view prefix) {
    std::string tmpl =
        absl::StrCat(StringFromEnv("TMPDIR", "/tmp"), "/", prefix, "XXXXXX");
    char* created = mkdtemp(tmpl.data());
    CF_EXPECTF(created != nullptr, "mkdtemp('{}') failed: {}", tmpl,
               StrError(errno));
    return ScopedTempDir(std::string(created));
  }

  ScopedTempDir() = default;
  ScopedTempDir(ScopedTempDir&& other) noexcept
      : path_(std::exchange(other.path_, {})) {}
  ScopedTempDir& operator=(ScopedTempDir&& other) noexcept {
    if (this != &other) {
      Cleanup();
      path_ = std::exchange(other.path_, {});
    }
    return *this;
  }
  ~ScopedTempDir() { Cleanup(); }

  const std::string& path() const { return path_; }

 private:
  explicit ScopedTempDir(std::string path) : path_(std::move(path)) {}

  void Cleanup() {
    if (!path_.empty()) {
      (void)RecursivelyRemoveDirectory(path_);
      path_.clear();
    }
  }

  std::string path_;
};

Result<std::vector<uint8_t>> ReadSparseSuperPrefix(sparse_file* sf,
                                                   size_t prefix_len) {
  struct PrefixContext {
    std::vector<uint8_t> buf;
    size_t offset = 0;
  };
  PrefixContext ctx{.buf = std::vector<uint8_t>(prefix_len, 0)};
  int rc = sparse_file_callback(
      sf, /* sparse= */ false, /* crc= */ false,
      [](void* priv, const void* data, size_t len) -> int {
        auto* c = static_cast<PrefixContext*>(priv);
        if (c->offset < c->buf.size() && data != nullptr) {
          const size_t to_copy = std::min(len, c->buf.size() - c->offset);
          memcpy(c->buf.data() + c->offset, data, to_copy);
        }
        c->offset += len;
        return c->offset >= c->buf.size() ? -ECANCELED : 0;
      },
      &ctx);
  CF_EXPECTF(rc == 0 || rc == -ECANCELED,
             "sparse_file_callback failed with rc={}", rc);
  ctx.buf.resize(std::min(ctx.offset, ctx.buf.size()));
  return std::move(ctx.buf);
}

class FdPartitionOpener final : public android::fs_mgr::PartitionOpener {
 public:
  explicit FdPartitionOpener(int fd) : fd_(fd) {}

  android::base::unique_fd Open(const std::string&, int) const override {
    return android::base::unique_fd(dup(fd_));
  }

 private:
  int fd_;
};

Result<void> UnsparseToRawFile(const std::string& src_path,
                               const std::string& dst_path) {
  android::base::unique_fd in_fd(open(src_path.c_str(), O_RDONLY | O_CLOEXEC));
  CF_EXPECTF(in_fd.ok(), "Failed to open sparse image '{}'", src_path);
  std::unique_ptr<sparse_file, decltype(&sparse_file_destroy)> sf(
      sparse_file_import(in_fd.get(), /* verbose= */ false, /* crc= */ false),
      sparse_file_destroy);
  CF_EXPECTF(sf != nullptr, "sparse_file_import failed on '{}'", src_path);

  android::base::unique_fd out_fd(
      open(dst_path.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644));
  CF_EXPECTF(out_fd.ok(), "Failed to open output raw image '{}'", dst_path);
  CF_EXPECT_EQ(sparse_file_write(sf.get(), out_fd.get(), /* gz= */ false,
                                 /* sparse= */ false, /* crc= */ false),
               0);
  return {};
}

Result<std::unique_ptr<android::fs_mgr::LpMetadata>> ReadSuperMetadata(
    const std::string& super_path) {
  if (!CF_EXPECT(IsSparseImage(super_path))) {
    if (android::fs_mgr::IsEmptySuperImage(super_path)) {
      std::unique_ptr<android::fs_mgr::LpMetadata> metadata =
          android::fs_mgr::ReadFromImageFile(super_path);
      CF_EXPECTF(metadata != nullptr, "Failed to parse empty super image '{}'",
                 super_path);
      return std::move(metadata);
    }
    std::unique_ptr<android::fs_mgr::LpMetadata> metadata =
        android::fs_mgr::ReadMetadata(super_path, 0);
    CF_EXPECTF(metadata != nullptr, "Failed to parse raw super image '{}'",
               super_path);
    return std::move(metadata);
  }

  android::base::unique_fd raw_fd(
      open(super_path.c_str(), O_RDONLY | O_CLOEXEC));
  CF_EXPECTF(raw_fd.ok(), "Failed to open sparse super image '{}'", super_path);
  std::unique_ptr<sparse_file, decltype(&sparse_file_destroy)> sf(
      sparse_file_import(raw_fd.get(), /* verbose= */ false, /* crc= */ false),
      sparse_file_destroy);
  CF_EXPECTF(sf != nullptr, "sparse_file_import failed on '{}'", super_path);

  static constexpr size_t kInitialPrefixBytes = 512 * 1024;
  static constexpr uint64_t kMaxSuperMetadataBytes = 64ULL * 1024 * 1024;
  std::vector<uint8_t> prefix =
      CF_EXPECT(ReadSparseSuperPrefix(sf.get(), kInitialPrefixBytes));
  CF_EXPECT_GE(prefix.size(),
               LP_PARTITION_RESERVED_BYTES + sizeof(LpMetadataGeometry));
  LpMetadataGeometry geometry{};
  memcpy(&geometry, prefix.data() + LP_PARTITION_RESERVED_BYTES,
         sizeof(geometry));
  if (geometry.magic != LP_METADATA_GEOMETRY_MAGIC &&
      prefix.size() >= LP_PARTITION_RESERVED_BYTES + LP_METADATA_GEOMETRY_SIZE +
                           sizeof(LpMetadataGeometry)) {
    memcpy(
        &geometry,
        prefix.data() + LP_PARTITION_RESERVED_BYTES + LP_METADATA_GEOMETRY_SIZE,
        sizeof(geometry));
  }
  CF_EXPECT_EQ(geometry.magic, uint32_t{LP_METADATA_GEOMETRY_MAGIC});

  const uint64_t per_copy_bytes =
      LP_METADATA_GEOMETRY_SIZE +
      static_cast<uint64_t>(geometry.metadata_max_size) *
          geometry.metadata_slot_count;
  const uint64_t required_bytes =
      LP_PARTITION_RESERVED_BYTES + per_copy_bytes * 2;
  CF_EXPECT_LE(required_bytes, kMaxSuperMetadataBytes);
  if (required_bytes > prefix.size()) {
    prefix = CF_EXPECT(
        ReadSparseSuperPrefix(sf.get(), static_cast<size_t>(required_bytes)));
  }

  std::unique_ptr<FILE, int (*)(FILE*)> mem_file(tmpfile(), fclose);
  CF_EXPECT(mem_file != nullptr, "tmpfile() failed");
  CF_EXPECT_EQ(fwrite(prefix.data(), 1, prefix.size(), mem_file.get()),
               prefix.size());
  CF_EXPECT_EQ(fflush(mem_file.get()), 0);

  FdPartitionOpener opener(fileno(mem_file.get()));
  std::unique_ptr<android::fs_mgr::LpMetadata> metadata =
      android::fs_mgr::ReadMetadata(opener, "super", 0);
  CF_EXPECTF(metadata != nullptr, "Failed to parse sparse super image '{}'",
             super_path);
  return std::move(metadata);
}

Result<void> PReadExact(const Fd& fd, void* buf, size_t size, uint64_t offset) {
  auto* out = static_cast<uint8_t*>(buf);
  size_t done = 0;
  while (done < size) {
    uint64_t got = CF_EXPECT(fd.PRead(out + done, size - done, offset + done));
    CF_EXPECT_GT(got, 0u);
    done += static_cast<size_t>(got);
  }
  return {};
}

bool RawSuperPartitionMatchesFile(const android::fs_mgr::LpMetadata& metadata,
                                  const LpMetadataPartition& part,
                                  const std::string& super_path,
                                  const Fd& raw_image_fd, uint64_t raw_size,
                                  uint64_t expected_aligned_size) {
  uint64_t total_extent_bytes = 0;
  for (uint32_t i = 0; i < part.num_extents; i++) {
    const size_t extent_index =
        static_cast<size_t>(part.first_extent_index) + i;
    if (extent_index >= metadata.extents.size()) {
      return false;
    }
    const LpMetadataExtent& extent = metadata.extents[extent_index];
    if (extent.target_type != LP_TARGET_TYPE_LINEAR ||
        extent.target_source != 0) {
      return false;
    }
    total_extent_bytes += extent.num_sectors * LP_SECTOR_SIZE;
  }
  if (total_extent_bytes != expected_aligned_size) {
    return false;
  }

  Result<Fd, int> super_fd_res = Fd::Open(super_path, O_RDONLY);
  if (!super_fd_res.has_value()) {
    return false;
  }
  const Fd& super_fd = *super_fd_res;

  std::vector<uint8_t> super_buf(64 * 1024);
  std::vector<uint8_t> image_buf(64 * 1024);
  uint64_t file_pos = 0;
  for (uint32_t i = 0; i < part.num_extents; i++) {
    const LpMetadataExtent& extent =
        metadata.extents[static_cast<size_t>(part.first_extent_index) + i];
    uint64_t super_off = extent.target_data * LP_SECTOR_SIZE;
    uint64_t ext_rem = extent.num_sectors * LP_SECTOR_SIZE;
    while (ext_rem > 0) {
      const size_t chunk =
          static_cast<size_t>(std::min<uint64_t>(ext_rem, super_buf.size()));
      if (!PReadExact(super_fd, super_buf.data(), chunk, super_off)
               .has_value()) {
        return false;
      }
      size_t file_chunk = 0;
      if (file_pos < raw_size) {
        file_chunk =
            static_cast<size_t>(std::min<uint64_t>(chunk, raw_size - file_pos));
        if (!PReadExact(raw_image_fd, image_buf.data(), file_chunk, file_pos)
                 .has_value()) {
          return false;
        }
        if (memcmp(super_buf.data(), image_buf.data(), file_chunk) != 0) {
          return false;
        }
      }
      for (size_t z = file_chunk; z < chunk; z++) {
        if (super_buf[z] != 0) {
          return false;
        }
      }
      super_off += chunk;
      file_pos += chunk;
      ext_rem -= chunk;
    }
  }
  return true;
}

bool DefaultPreferSystemGroup(std::string_view partition_name) {
  return partition_name == "boot" || partition_name == "init_boot" ||
         partition_name == "vbmeta" || partition_name == "vbmeta_system" ||
         partition_name == "vbmeta_system_dlkm" ||
         partition_name == "system_dlkm" || partition_name == "system" ||
         partition_name == "system_ext" || partition_name == "product";
}

}  // namespace

Result<bool> SuperHasPartition(const std::string& superimg_path,
                               std::string_view partition_name) {
  if (superimg_path.empty() || !FileExists(superimg_path)) {
    return false;
  }
  std::unique_ptr<android::fs_mgr::LpMetadata> metadata =
      CF_EXPECT(ReadSuperMetadata(superimg_path));
  const std::string exact(partition_name);
  const std::string slot_a = absl::StrCat(partition_name, "_a");
  return android::fs_mgr::FindPartition(*metadata, exact) != nullptr ||
         android::fs_mgr::FindPartition(*metadata, slot_a) != nullptr;
}

// Steps for building a vendor_dlkm.img:
// 1. Generate filesystem_config.txt , which contains standard linux file
// permissions, we use 0755 for directories, and 0644 for all files
// 2. Write file_contexts, which contains all selinux labels
// 3. Call  sefcontext_compile to compile file_contexts
// 4. call mkuserimg_mke2fs to build an image, using filesystem_config and
// file_contexts previously generated
// 5. call avbtool to add hashtree footer, so that init/bootloader can verify
// AVB chain
Result<void> BuildDlkmImage(const std::string& src_dir, const bool is_erofs,
                            const std::string& partition_name,
                            const std::string& output_image) {
  CF_EXPECT(!is_erofs,
            "Building DLKM image in EROFS format is currently not supported!");

  const std::string mount_point = "/" + partition_name;
  const std::string fs_config = output_image + ".fs_config";
  CF_EXPECT(WriteFsConfig(fs_config, src_dir, mount_point));

  const std::string file_contexts_bin = output_image + ".file_contexts";
  if (partition_name == "system_dlkm") {
    CF_EXPECT(GenerateFileContexts(file_contexts_bin, mount_point,
                                   "system_dlkm_file"));
  } else {
    CF_EXPECT(
        GenerateFileContexts(file_contexts_bin, mount_point, "vendor_file"));
  }

  // We are using directory size as an estimate of final image size. To avoid
  // any rounding errors, add 16M of head room.
  const auto fs_size =
      RoundUp(CF_EXPECT(GetDiskUsageBytes(src_dir)) + 16 * 1024 * 1024, 4096);
  LOG(INFO) << mount_point << " src dir " << src_dir << " has size "
            << fs_size / 1024 << " KB";

  Command mkfs_cmd =
      Command(MkuserimgMke2fsBinary())
          // Arbitrary UUID/seed, just to keep output consistent between runs
          .AddParameter("--mke2fs_uuid")
          .AddParameter("cb09b942-ed4e-46a1-81dd-7d535bf6c4b1")
          .AddParameter("--mke2fs_hash_seed")
          .AddParameter("765d8aba-d93f-465a-9fcf-14bb794eb7f4")
          // Arbitrary date, just to keep output consistent
          .AddParameter("-T")
          .AddParameter(900979200000)
          // selinux permission to keep selinux happy
          .AddParameter("--fs_config")
          .AddParameter(fs_config)

          .AddParameter(src_dir)
          .AddParameter(output_image)
          .AddParameter("ext4")
          .AddParameter(mount_point)
          .AddParameter(fs_size)
          .AddParameter(file_contexts_bin);

  CF_EXPECT_EQ(mkfs_cmd.Start().Wait(), 0,
               "Failed to build vendor_dlkm ext4 image");
  CF_EXPECT(AddVbmetaFooter(output_image, partition_name));

  return {};
}

Result<void> RepackSuperWithPartition(const std::string& superimg_path,
                                      const std::string& image_path,
                                      const std::string& partition_name) {
  CF_EXPECTF(FileExists(superimg_path), "Super image '{}' does not exist",
             superimg_path);
  CF_EXPECTF(FileExists(image_path), "Partition image '{}' does not exist",
             image_path);

  const std::string abs_super_path = AbsolutePath(superimg_path);
  if (CF_EXPECT(IsSparseImage(abs_super_path))) {
    const std::string tmp_super = absl::StrCat(abs_super_path, ".raw.tmp");
    (void)RemoveFile(tmp_super);
    Result<void> conv_res = UnsparseToRawFile(abs_super_path, tmp_super);
    if (!conv_res.has_value()) {
      (void)RemoveFile(tmp_super);
    }
    CF_EXPECT(std::move(conv_res));
    Result<void, int> rename_res = Rename(tmp_super, abs_super_path);
    if (!rename_res.has_value()) {
      (void)RemoveFile(tmp_super);
    }
    CF_EXPECT(std::move(rename_res));
  }

  ScopedTempDir temp_dir;
  std::string raw_image_path = image_path;
  if (CF_EXPECT(IsSparseImage(image_path))) {
    temp_dir = CF_EXPECT(ScopedTempDir::Create("cf_repack_super_"));
    raw_image_path = absl::StrCat(temp_dir.path(), "/partition.raw");
    CF_EXPECT(UnsparseToRawFile(image_path, raw_image_path));
  }

  std::unique_ptr<android::fs_mgr::LpMetadata> metadata =
      android::fs_mgr::ReadMetadata(abs_super_path, 0);
  CF_EXPECTF(metadata != nullptr, "Failed to read super metadata from '{}'",
             abs_super_path);

  std::string target_name = absl::StrCat(partition_name, "_a");
  if (android::fs_mgr::FindPartition(*metadata, target_name) == nullptr &&
      android::fs_mgr::FindPartition(*metadata, partition_name) != nullptr) {
    target_name = partition_name;
  }

  Fd raw_image_fd = CF_EXPECT(Fd::Open(raw_image_path, O_RDONLY));
  const uint64_t raw_size = CF_EXPECT(raw_image_fd.SeekEnd(0));
  CF_EXPECT(raw_image_fd.SeekSet(0));

  const uint64_t block_size =
      std::max<uint64_t>(metadata->geometry.logical_block_size, LP_SECTOR_SIZE);
  CF_EXPECT_GE(raw_size + block_size - 1, raw_size);
  const uint64_t aligned_size =
      ((raw_size + block_size - 1) / block_size) * block_size;

  if (const LpMetadataPartition* existing_raw =
          android::fs_mgr::FindPartition(*metadata, target_name);
      existing_raw != nullptr &&
      RawSuperPartitionMatchesFile(*metadata, *existing_raw, abs_super_path,
                                   raw_image_fd, raw_size, aligned_size)) {
    return {};
  }

  std::unique_ptr<android::fs_mgr::MetadataBuilder> builder =
      android::fs_mgr::MetadataBuilder::New(*metadata);
  CF_EXPECTF(builder != nullptr, "MetadataBuilder::New failed on '{}'",
             abs_super_path);

  std::string group_name = "default";
  uint32_t attributes = LP_PARTITION_ATTR_READONLY;
  if (android::fs_mgr::Partition* existing =
          builder->FindPartition(target_name);
      existing != nullptr) {
    group_name = existing->group_name();
    attributes = existing->attributes();
    builder->RemovePartition(target_name);
  } else {
    const bool prefer_system = DefaultPreferSystemGroup(partition_name);
    const std::string preferred = prefer_system
                                      ? "google_system_dynamic_partitions_a"
                                      : "google_vendor_dynamic_partitions_a";
    const std::string alternate = prefer_system
                                      ? "google_vendor_dynamic_partitions_a"
                                      : "google_system_dynamic_partitions_a";
    if (builder->FindGroup(preferred) != nullptr) {
      group_name = preferred;
    } else if (builder->FindGroup(alternate) != nullptr) {
      group_name = alternate;
    }
  }

  android::fs_mgr::Partition* part =
      CF_EXPECT(builder->AddPartition(target_name, group_name, attributes));
  CF_EXPECTF(builder->ResizePartition(part, raw_size),
             "Failed to resize '{}' to {} bytes in '{}'", target_name, raw_size,
             abs_super_path);

  std::unique_ptr<android::fs_mgr::LpMetadata> exported = builder->Export();
  CF_EXPECT(exported != nullptr, "Failed to export updated super metadata");

  const uint32_t slots =
      std::min<uint32_t>(exported->geometry.metadata_slot_count, 2u);
  for (uint32_t slot = 0; slot < slots; slot++) {
    CF_EXPECTF(
        android::fs_mgr::UpdatePartitionTable(abs_super_path, *exported, slot),
        "Failed to update metadata slot {} in '{}'", slot, abs_super_path);
  }

  const LpMetadataPartition* updated_part =
      android::fs_mgr::FindPartition(*exported, target_name);
  CF_EXPECT(updated_part != nullptr);

  Fd super_fd = CF_EXPECT(Fd::Open(abs_super_path, O_RDWR));
  CF_EXPECT(raw_image_fd.SeekSet(0));

  uint64_t file_pos = 0;
  std::vector<char> zeroes(64 * 1024, 0);
  for (uint32_t i = 0; i < updated_part->num_extents; i++) {
    const size_t extent_index =
        static_cast<size_t>(updated_part->first_extent_index) + i;
    CF_EXPECT_LT(extent_index, exported->extents.size());
    const LpMetadataExtent& extent = exported->extents[extent_index];
    CF_EXPECT_EQ(extent.target_type, uint32_t{LP_TARGET_TYPE_LINEAR});
    CF_EXPECT_EQ(extent.target_source, 0u);

    const uint64_t dst_offset = extent.target_data * LP_SECTOR_SIZE;
    uint64_t ext_bytes = extent.num_sectors * LP_SECTOR_SIZE;
    CF_EXPECT(super_fd.SeekSet(dst_offset));
    if (file_pos < raw_size) {
      const uint64_t copy_bytes = std::min(raw_size - file_pos, ext_bytes);
      CF_EXPECT(super_fd.CopyFrom(raw_image_fd, copy_bytes));
      file_pos += copy_bytes;
      ext_bytes -= copy_bytes;
    }
    while (ext_bytes > 0) {
      const size_t z_len =
          static_cast<size_t>(std::min<uint64_t>(ext_bytes, zeroes.size()));
      CF_EXPECT(WriteExact(super_fd, zeroes.data(), z_len));
      ext_bytes -= z_len;
    }
  }
  CF_EXPECT_EQ(file_pos, raw_size);
  return {};
}

Result<void> BuildVbmetaImage(const std::string& vendor_dlkm_img,
                              const std::string& vbmeta_path) {
  CF_EXPECT(!vendor_dlkm_img.empty());
  CF_EXPECTF(FileExists(vendor_dlkm_img), "'{}' does not exist",
             vendor_dlkm_img);

  CF_EXPECT(Avb().MakeVbMetaImage(vbmeta_path, {}, {vendor_dlkm_img},
                                  {"--padding_size", "4096"}));
  return {};
}

Result<void> SplitRamdiskModules(const std::string& ramdisk_path,
                                 const std::string& ramdisk_stage_dir,
                                 const std::string& vendor_dlkm_build_dir,
                                 const std::string& system_dlkm_build_dir) {
  std::string vendor_modules_dir = vendor_dlkm_build_dir + "/lib/modules";
  std::string system_modules_dir = system_dlkm_build_dir + "/lib/modules";

  CF_EXPECT(EnsureDirectoryExists(vendor_modules_dir));
  CF_EXPECT(EnsureDirectoryExists(system_modules_dir));

  CF_EXPECT(UnpackRamdisk(ramdisk_path, ramdisk_stage_dir));

  std::string module_load_file =
      CF_EXPECT(FindFile(ramdisk_stage_dir, "modules.load"),
                "Failed to find modules.dep file in input ramdisk");
  module_load_file = std::string(absl::StripAsciiWhitespace(module_load_file));

  CF_EXPECTF(!module_load_file.empty(),
             "Failed to find modules.dep file in input ramdisk '{}'",
             ramdisk_path);

  LOG(INFO) << "modules.load location " << module_load_file;
  const std::set<std::string> module_list =
      absl::StrSplit(ReadFile(module_load_file), '\n', absl::SkipEmpty());
  const auto module_base_dir = android::base::Dirname(module_load_file);
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

    const std::string module_location =
        absl::StrCat(module_base_dir, "/", module_path);
    if (!FileExists(module_location)) {
      continue;
    }
    Fd module_fd = CF_EXPECT(Fd::Open(module_location, O_RDONLY));

    if (IsKernelModuleSigned(module_fd).value_or(false)) {
      const auto system_dlkm_module_location =
          fmt::format("{}/{}", system_modules_dir, module_path);

      CF_EXPECT(EnsureDirectoryExists(
          android::base::Dirname(system_dlkm_module_location)));
      CF_EXPECT(RenameFile(module_location, system_dlkm_module_location));

      system_dlkm_modules.emplace(module_path);
    } else {
      const auto vendor_dlkm_module_location =
          fmt::format("{}/{}", vendor_modules_dir, module_path);

      CF_EXPECT(EnsureDirectoryExists(
          android::base::Dirname(vendor_dlkm_module_location)));
      CF_EXPECT(RenameFile(module_location, vendor_dlkm_module_location));

      vendor_dlkm_modules.emplace(module_path);
    }
  }
  for (const auto& gki_module : system_dlkm_modules) {
    for (const auto& dep : deps.at(gki_module)) {
      CF_EXPECTF(vendor_dlkm_modules.count(dep) == 0,
                 "GKI module '{}' depends on vendor_dlkm module '{}'",
                 gki_module, dep);
    }
  }
  LOG(INFO) << "There are " << ramdisk_modules.size() << " ramdisk modules, "
            << vendor_dlkm_modules.size() << " vendor_dlkm modules, "
            << system_dlkm_modules.size() << " system_dlkm modules.";

  // transfer blocklist in whole to the vendor dlkm partition. It currently
  // only contains one module that is loaded during second stage init.
  // We can split the blocklist at a later date IF it contains modules in
  // different partitions.
  const auto initramfs_blocklist_path = module_base_dir + "/modules.blocklist";
  if (FileExists(initramfs_blocklist_path)) {
    const auto vendor_dlkm_blocklist_path =
        fmt::format("{}/{}", vendor_modules_dir, "modules.blocklist");
    CF_EXPECT(RenameFile(initramfs_blocklist_path, vendor_dlkm_blocklist_path));
  }

  // Write updated modules.dep and modules.load files
  CF_EXPECT(WriteDepsToFile(FilterDependencies(deps, ramdisk_modules),
                            module_base_dir + "/modules.dep"));
  CF_EXPECT(WriteLinesToFile(ramdisk_modules, module_load_file));

  CF_EXPECT(WriteDepsToFile(
      UpdateGKIModulePaths(FilterOutDependencies(deps, ramdisk_modules),
                           system_dlkm_modules),
      vendor_modules_dir + "/modules.dep"));
  CF_EXPECT(WriteLinesToFile(vendor_dlkm_modules,
                             vendor_modules_dir + "/modules.load"));

  CF_EXPECT(WriteDepsToFile(FilterDependencies(deps, system_dlkm_modules),
                            system_modules_dir + "/modules.dep"));
  CF_EXPECT(WriteLinesToFile(system_dlkm_modules,
                             system_modules_dir + "/modules.load"));
  CF_EXPECT(PackRamdisk(ramdisk_stage_dir, ramdisk_path));
  return {};
}

bool FileEquals(const std::string& file1, const std::string& file2) {
  if (FileSize(file1) != FileSize(file2)) {
    return false;
  }
  std::array<uint8_t, 1024 * 16> buf1{};
  std::array<uint8_t, 1024 * 16> buf2{};
  Fd fd1 = Fd::Open(file1, O_RDONLY).value_or(Fd());
  Fd fd2 = Fd::Open(file2, O_RDONLY).value_or(Fd());
  auto bytes_remain = FileSize(file1);
  while (bytes_remain > 0) {
    const auto bytes_to_read = std::min<size_t>(bytes_remain, buf1.size());
    if (fd1.Read(buf1.data(), bytes_to_read) != bytes_to_read) {
      LOG(ERROR) << "Failed to read from " << file1;
      return false;
    }
    if (!fd2.Read(buf2.data(), bytes_to_read)) {
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
  if (!ret.has_value()) {
    LOG(ERROR) << ret.error();
    return false;
  }
  return true;
}

}  // namespace cuttlefish
