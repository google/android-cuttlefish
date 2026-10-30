//
// Copyright (C) 2025 The Android Open Source Project
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

#include "cuttlefish/host/commands/assemble_cvd/android_build/super_image.h"

#include <errno.h>
#include <fcntl.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <algorithm>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/strip.h"
#include "android-base/unique_fd.h"
#include "liblp/liblp.h"
#include "liblp/metadata_format.h"
#include "liblp/partition_opener.h"
#include "sparse/sparse.h"

#include "cuttlefish/common/libs/fs/fd.h"
#include "cuttlefish/common/libs/fs/shared_fd.h"
#include "cuttlefish/common/libs/utils/files.h"
#include "cuttlefish/files/file_exists.h"
#include "cuttlefish/host/commands/assemble_cvd/android_build/android_build.h"
#include "cuttlefish/host/libs/image_aggregator/sparse_image.h"
#include "cuttlefish/io/write_exact.h"
#include "cuttlefish/pretty/liblp/liblp.h"  // IWYU pragma: keep: overloads
#include "cuttlefish/pretty/map.h"          // IWYU pragma: keep: overloads
#include "cuttlefish/pretty/result.h"       // IWYU pragma: keep: overloads
#include "cuttlefish/pretty/set.h"          // IWYU pragma: keep: overloads
#include "cuttlefish/pretty/struct.h"
#include "cuttlefish/pretty/unique_ptr.h"  // IWYU pragma: keep: overloads
#include "cuttlefish/result/result.h"

namespace cuttlefish {
namespace {

constexpr size_t kInitialSuperPrefixBytes = 512 * 1024;
constexpr uint64_t kMaxSuperMetadataBytes = 64ULL * 1024 * 1024;

bool HasSlotSuffix(const std::string_view name) {
  return absl::EndsWith(name, "_a") || absl::EndsWith(name, "_b");
}

std::string WithoutSlotSuffix(std::string_view name) {
  absl::ConsumeSuffix(&name, "_a");
  absl::ConsumeSuffix(&name, "_b");
  return std::string(name);
}

Result<std::vector<LpMetadataExtent>> PartitionExtents(
    const android::fs_mgr::LpMetadata& metadata, std::string_view name) {
  const LpMetadataPartition* matched =
      android::fs_mgr::FindPartition(metadata, std::string(name));
  if (matched == nullptr && !HasSlotSuffix(name)) {
    matched =
        android::fs_mgr::FindPartition(metadata, absl::StrCat(name, "_a"));
  }
  if (matched == nullptr) {
    for (const LpMetadataPartition& partition : metadata.partitions) {
      std::string partition_name = android::fs_mgr::GetPartitionName(partition);
      if (name == WithoutSlotSuffix(partition_name)) {
        matched = &partition;
        break;
      }
    }
  }
  CF_EXPECTF(matched != nullptr, "Could not find partition with name '{}'",
             name);
  std::vector<LpMetadataExtent> extents;
  for (uint32_t i = 0; i < matched->num_extents; i++) {
    const size_t extent_index =
        static_cast<size_t>(matched->first_extent_index) + i;
    CF_EXPECT_LT(extent_index, metadata.extents.size());
    extents.emplace_back(metadata.extents[extent_index]);
  }
  return extents;
}

Result<uint64_t> TotalExtentsSizeBytes(
    const std::vector<LpMetadataExtent>& extents) {
  uint64_t total = 0;
  for (const LpMetadataExtent& extent : extents) {
    const uint64_t length = extent.num_sectors * LP_SECTOR_SIZE;
    CF_EXPECT_GE(total + length, total);
    total += length;
  }
  return total;
}

Result<std::vector<uint8_t>> ReadSparsePrefix(sparse_file* sf,
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

Result<std::unique_ptr<android::fs_mgr::LpMetadata>> ReadMetadataFromSuperFile(
    const std::string& super_path) {
  if (!CF_EXPECT(IsSparseImage(super_path))) {
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

  std::vector<uint8_t> prefix =
      CF_EXPECT(ReadSparsePrefix(sf.get(), kInitialSuperPrefixBytes));
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
        ReadSparsePrefix(sf.get(), static_cast<size_t>(required_bytes)));
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

struct LinearSpan {
  uint64_t src_offset;
  uint64_t src_end;
  uint64_t dst_offset;
};

Result<void> ExtractPartition(const std::string& super_path,
                              const android::fs_mgr::LpMetadata& metadata,
                              const std::vector<LpMetadataExtent>& extents,
                              Fd& destination) {
  const LpMetadataBlockDevice* super_device =
      android::fs_mgr::GetMetadataSuperBlockDevice(metadata);
  CF_EXPECT(super_device != nullptr, "Super metadata missing block device");
  const std::string super_bdev_name =
      android::fs_mgr::GetBlockDevicePartitionName(*super_device);

  uint64_t total_dst_bytes = 0;
  uint64_t max_src_end = 0;
  std::vector<LinearSpan> spans;
  for (const LpMetadataExtent& extent : extents) {
    const uint64_t length = extent.num_sectors * LP_SECTOR_SIZE;
    switch (extent.target_type) {
      case LP_TARGET_TYPE_LINEAR: {
        CF_EXPECT_LT(extent.target_source, metadata.block_devices.size());
        CF_EXPECT_EQ(android::fs_mgr::GetBlockDevicePartitionName(
                         metadata.block_devices[extent.target_source]),
                     super_bdev_name);
        const uint64_t src_offset = extent.target_data * LP_SECTOR_SIZE;
        const uint64_t src_end = src_offset + length;
        CF_EXPECT_GE(src_end, src_offset);
        if (length > 0) {
          spans.push_back(LinearSpan{
              .src_offset = src_offset,
              .src_end = src_end,
              .dst_offset = total_dst_bytes,
          });
          max_src_end = std::max(max_src_end, src_end);
        }
        break;
      }
      case LP_TARGET_TYPE_ZERO:
        break;
      default:
        return CF_ERRF("Unknown target_type '{}'", extent.target_type);
    }
    CF_EXPECT_GE(total_dst_bytes + length, total_dst_bytes);
    total_dst_bytes += length;
  }

  CF_EXPECT(destination.SeekSet(0));
  CF_EXPECT(destination.Truncate(total_dst_bytes));
  if (spans.empty()) {
    return {};
  }

  if (!CF_EXPECT(IsSparseImage(super_path))) {
    Fd source = CF_EXPECT(Fd::Open(super_path, O_RDONLY));
    for (const LinearSpan& span : spans) {
      CF_EXPECT(source.SeekSet(span.src_offset));
      CF_EXPECT(destination.SeekSet(span.dst_offset));
      CF_EXPECT(destination.CopyFrom(source, span.src_end - span.src_offset));
    }
    return {};
  }

  android::base::unique_fd raw_fd(
      open(super_path.c_str(), O_RDONLY | O_CLOEXEC));
  CF_EXPECTF(raw_fd.ok(), "Failed to open sparse super image '{}'", super_path);
  std::unique_ptr<sparse_file, decltype(&sparse_file_destroy)> sf(
      sparse_file_import(raw_fd.get(), /* verbose= */ false, /* crc= */ false),
      sparse_file_destroy);
  CF_EXPECTF(sf != nullptr, "sparse_file_import failed on '{}'", super_path);

  struct ExtractContext {
    const std::vector<LinearSpan>* spans;
    uint64_t max_src_end;
    Fd* destination;
    uint64_t cursor = 0;
    Result<void> status;
  };
  ExtractContext ctx{
      .spans = &spans,
      .max_src_end = max_src_end,
      .destination = &destination,
  };

  int rc = sparse_file_callback(
      sf.get(), /* sparse= */ false, /* crc= */ false,
      [](void* priv, const void* data, size_t len) -> int {
        auto* c = static_cast<ExtractContext*>(priv);
        const uint64_t chunk_start = c->cursor;
        const uint64_t chunk_end = chunk_start + len;
        c->cursor = chunk_end;
        if (data != nullptr) {
          const auto* bytes = static_cast<const char*>(data);
          for (const LinearSpan& span : *c->spans) {
            const uint64_t ov_start = std::max(chunk_start, span.src_offset);
            const uint64_t ov_end = std::min(chunk_end, span.src_end);
            if (ov_start >= ov_end) {
              continue;
            }
            const uint64_t dst_pos =
                span.dst_offset + (ov_start - span.src_offset);
            if (auto seek_res = c->destination->SeekSet(dst_pos);
                !seek_res.has_value()) {
              c->status = CF_ERR("Failed to seek destination file");
              return -EIO;
            }
            const size_t write_len = static_cast<size_t>(ov_end - ov_start);
            if (auto write_res =
                    WriteExact(*c->destination,
                               bytes + (ov_start - chunk_start), write_len);
                !write_res.has_value()) {
              c->status = std::move(write_res);
              return -EIO;
            }
          }
        }
        return chunk_end >= c->max_src_end ? -ECANCELED : 0;
      },
      &ctx);
  CF_EXPECT(std::move(ctx.status));
  CF_EXPECTF(rc == 0 || rc == -ECANCELED,
             "sparse_file_callback failed with rc={}", rc);
  CF_EXPECT_GE(ctx.cursor, max_src_end);
  return {};
}

class SuperImageAsBuildImpl : public AndroidBuild {
 public:
  SuperImageAsBuildImpl(
      AndroidBuild& android_build,
      std::unique_ptr<android::fs_mgr::LpMetadata> super_metadata)
      : android_build_(&android_build),
        super_metadata_(std::move(super_metadata)) {}

  std::string Name() const override { return "SuperImageAsBuild"; }

  PrettyStruct Pretty() override {
    return PrettyStruct(Name())
        .Member("Images()", Images())
        .Member("LogicalPartitions()", LogicalPartitions())
        .Member("AbPartitions()", AbPartitions())
        .Member("SystemPartitions()", SystemPartitions())
        .Member("VendorPartitions()", VendorPartitions())
        .Member("super_metadata_", super_metadata_)
        .Member("extracted_", extracted_)
        .Member("extract_dir", extract_dir_);
  }

  Result<std::set<std::string, std::less<void>>> Images() override {
    std::set<std::string, std::less<void>> images =
        CF_EXPECT(android_build_->Images());
    CF_EXPECT(images.count("super"), "Can't extract from super_empty");
    return CF_EXPECT(LogicalPartitions());
  }

  Result<std::string> ImageFile(std::string_view name, bool extract) override {
    if (auto it = extracted_.find(name);
        it != extracted_.end() && FileExists(it->second)) {
      return it->second;
    }

    CF_EXPECT(super_metadata_.get());
    std::vector<LpMetadataExtent> extents =
        CF_EXPECT(PartitionExtents(*super_metadata_, name));
    const uint64_t expected_size = CF_EXPECT(TotalExtentsSizeBytes(extents));

    if (!extract_dir_.empty()) {
      std::string candidate = absl::StrCat(extract_dir_, "/", name, ".img");
      if (FileExists(candidate) &&
          static_cast<uint64_t>(FileSize(candidate)) == expected_size) {
        Result<std::string> super_no_extract =
            android_build_->ImageFile("super", false);
        if (super_no_extract.has_value()) {
          auto cand_mtime = FileModificationTime(candidate);
          auto super_mtime = FileModificationTime(*super_no_extract);
          if (cand_mtime.has_value() && super_mtime.has_value() &&
              *cand_mtime >= *super_mtime) {
            extracted_[std::string(name)] = candidate;
            return candidate;
          }
        }
      }
    }

    CF_EXPECTF(!!extract, "'{}' was not already extracted", name);
    CF_EXPECT(!extract_dir_.empty(), "`SetExtractDir` was never called");

    std::string super_path =
        CF_EXPECT(android_build_->ImageFile("super", true));

    CF_EXPECT(EnsureDirectoryExists(extract_dir_));
    std::string extract_path = absl::StrCat(extract_dir_, "/", name, ".img");
    unlink(extract_path.c_str());  // Ignore errors
    Fd extract_fd =
        CF_EXPECT(Fd::Open(extract_path, O_RDWR | O_CREAT | O_EXCL, 0644));

    CF_EXPECT(
        ExtractPartition(super_path, *super_metadata_, extents, extract_fd));
    extracted_[std::string(name)] = extract_path;

    return extract_path;
  }

  Result<void> SetExtractDir(std::string_view extract_dir) override {
    if (extract_dir_ != extract_dir) {
      extracted_.clear();
      extract_dir_ = extract_dir;
    }
    return {};
  }

  Result<std::set<std::string, std::less<void>>> LogicalPartitions() override {
    std::set<std::string, std::less<void>> ret;
    for (const LpMetadataPartition& partition : super_metadata_->partitions) {
      std::string name = android::fs_mgr::GetPartitionName(partition);
      ret.emplace(WithoutSlotSuffix(name));
    }
    return ret;
  }

  Result<std::set<std::string, std::less<void>>> PartitionsInGroup(
      std::string_view match) {
    std::set<std::string, std::less<void>> ret;
    for (const LpMetadataPartition& partition : super_metadata_->partitions) {
      CF_EXPECT_LT(partition.group_index, super_metadata_->groups.size());
      std::string group_name = android::fs_mgr::GetPartitionGroupName(
          super_metadata_->groups[partition.group_index]);

      if (absl::StrContains(group_name, match)) {
        std::string name = android::fs_mgr::GetPartitionName(partition);
        ret.emplace(WithoutSlotSuffix(name));
      }
    }
    return ret;
  }

  Result<std::set<std::string, std::less<void>>> AbPartitions() override {
    std::set<std::string, std::less<void>> ret;
    for (const LpMetadataPartition& partition : super_metadata_->partitions) {
      std::string name = android::fs_mgr::GetPartitionName(partition);
      if (HasSlotSuffix(name)) {
        ret.emplace(WithoutSlotSuffix(name));
      }
    }
    return ret;
  }

  Result<std::set<std::string, std::less<void>>> SystemPartitions() override {
    return CF_EXPECT(PartitionsInGroup("system"));
  }

  Result<std::set<std::string, std::less<void>>> VendorPartitions() override {
    return CF_EXPECT(PartitionsInGroup("vendor"));
  }

 private:
  AndroidBuild* android_build_;
  std::unique_ptr<android::fs_mgr::LpMetadata> super_metadata_;
  std::map<std::string, std::string, std::less<void>> extracted_;
  std::string extract_dir_;
};

Result<std::unique_ptr<android::fs_mgr::LpMetadata>> SuperImageFromAndroidBuild(
    AndroidBuild& build) {
  static constexpr std::string_view kSuperEmpty = "super_empty";
  static constexpr std::string_view kSuper = "super";
  // Prefer an already extracted file, then prefer extracting super_empty since
  // it is smaller.
  Result<std::string> path;
  std::unique_ptr<android::fs_mgr::LpMetadata> metadata;
  if (path = build.ImageFile(kSuperEmpty); path.has_value()) {
    metadata = android::fs_mgr::ReadFromImageFile(*path);
  } else if (path = build.ImageFile(kSuper); path.has_value()) {
    metadata = CF_EXPECT(ReadMetadataFromSuperFile(*path));
  } else {
    return CF_ERR("No super.img or super_empty.img could be found");
  }
  CF_EXPECTF(metadata.get(), "Failed to parse super image '{}'", *path);
  return metadata;
}

}  // namespace

Result<std::unique_ptr<AndroidBuild>> SuperImageAsBuild(AndroidBuild& build) {
  std::unique_ptr<android::fs_mgr::LpMetadata> lp_metadata =
      CF_EXPECT(SuperImageFromAndroidBuild(build));

  auto super_build =
      std::make_unique<SuperImageAsBuildImpl>(build, std::move(lp_metadata));

  CF_EXPECT(super_build->SystemPartitions());
  CF_EXPECT(super_build->VendorPartitions());
  CF_EXPECT(super_build->LogicalPartitions());
  CF_EXPECT(super_build->AbPartitions());

  return super_build;
}

}  // namespace cuttlefish
