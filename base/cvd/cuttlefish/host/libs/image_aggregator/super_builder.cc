/*
 * Copyright (C) 2026 The Android Open Source Project
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
#include "cuttlefish/host/libs/image_aggregator/super_builder.h"

#include <stdint.h>

#include <openssl/sha.h>

#include <map>
#include <string_view>

#include "absl/strings/str_cat.h"
#include "liblp/builder.h"
#include "liblp/metadata_format.h"
#include "liblp/partition_opener.h"

#include "cuttlefish/common/libs/fs/shared_buf.h"
#include "cuttlefish/common/libs/fs/shared_fd.h"
#include "cuttlefish/host/libs/image_aggregator/cdisk_spec.pb.h"
#include "cuttlefish/host/libs/image_aggregator/composite_disk.h"
#include "cuttlefish/host/libs/image_aggregator/disk_image.h"
#include "cuttlefish/host/libs/image_aggregator/image_from_file.h"
#include "cuttlefish/result/result.h"

#include "cuttlefish/host/libs/config/log_string_to_dir.h"
#include "cuttlefish/pretty/liblp/liblp.h"  // IWYU pragma: keep: overloads
#include "cuttlefish/pretty/unique_ptr.h"

namespace cuttlefish {
namespace {

using android::fs_mgr::BlockDeviceInfo;
using android::fs_mgr::Interval;
using android::fs_mgr::LinearExtent;
using android::fs_mgr::LpMetadata;
using android::fs_mgr::MetadataBuilder;
using android::fs_mgr::Partition;

// `SHA256, ``SerializeGeometry` and `SerializeMetadata` are copied from the
// liblp implementation.
//
// liblp only exposes two methods of producing the header, in different
// overloads of `WriteToImageFile`. One overload produces the `super_empty.img`
// file with a single copy of the geometry and metadata tables. The other
// overload produces the `super.img` file with two copies of each table, and the
// contents of all the logical partitions.
//
// Technically we lose a call to `CheckExtentOrdering`, but we create the
// extents in ascending order.
//
// We want a mixture of the behavior of both overloads: two copies of each
// table, but without the logical partition contents. We are instead providing
// the logical partition contents through the composite disk indirection.
// Therefore, we copy these internal methods to serialize the tables so we can
// construct the `super.img` version of the header by itself.

void SHA256(const void* data, const size_t length, uint8_t out[32]) {
  SHA256_CTX c;
  SHA256_Init(&c);
  SHA256_Update(&c, data, length);
  SHA256_Final(out, &c);
}

std::string SerializeGeometry(LpMetadataGeometry& geometry) {
  memset(geometry.checksum, 0, sizeof(geometry.checksum));
  SHA256(&geometry, sizeof(geometry), geometry.checksum);

  std::string blob(reinterpret_cast<const char*>(&geometry), sizeof(geometry));
  blob.resize(LP_METADATA_GEOMETRY_SIZE);
  return blob;
}

std::string SerializeMetadata(LpMetadata& metadata) {
  LpMetadataHeader& header = metadata.header;

  // Serialize individual tables.
  const std::string partitions(
      reinterpret_cast<const char*>(metadata.partitions.data()),
      metadata.partitions.size() * sizeof(LpMetadataPartition));
  const std::string extents(
      reinterpret_cast<const char*>(metadata.extents.data()),
      metadata.extents.size() * sizeof(LpMetadataExtent));
  const std::string groups(
      reinterpret_cast<const char*>(metadata.groups.data()),
      metadata.groups.size() * sizeof(LpMetadataPartitionGroup));
  const std::string block_devices(
      reinterpret_cast<const char*>(metadata.block_devices.data()),
      metadata.block_devices.size() * sizeof(LpMetadataBlockDevice));

  // Compute positions of tables.
  header.partitions.offset = 0;
  header.extents.offset = header.partitions.offset + partitions.size();
  header.groups.offset = header.extents.offset + extents.size();
  header.block_devices.offset = header.groups.offset + groups.size();
  header.tables_size = header.block_devices.offset + block_devices.size();

  // Compute payload checksum.
  const std::string tables =
      absl::StrCat(partitions, extents, groups, block_devices);
  SHA256(tables.data(), tables.size(), header.tables_checksum);

  // Compute header checksum.
  memset(header.header_checksum, 0, sizeof(header.header_checksum));
  SHA256(&header, header.header_size, header.header_checksum);

  const std::string_view header_blob(reinterpret_cast<const char*>(&header),
                                     header.header_size);

  std::string ret = absl::StrCat(header_blob, tables);
  ret.resize(metadata.geometry.metadata_max_size);
  return ret;
}

BlockDeviceInfo DefaultBlockDeviceInfo(const uint64_t size) {
  static constexpr uint32_t kAlignment = 4096;
  static constexpr uint32_t kAlignmentOffset = 0;
  static constexpr uint32_t kLogicalBlockSize = 4096;

  return BlockDeviceInfo(LP_METADATA_DEFAULT_PARTITION_NAME, size, kAlignment,
                         kAlignmentOffset, kLogicalBlockSize);
}

Result<std::unique_ptr<MetadataBuilder>> CreateMetadataBuilder(uint64_t size) {
  static constexpr uint32_t kMetadataMaxSize = 256 * 1024;
  static constexpr uint32_t kMetadataSlotCount = 2;
  std::unique_ptr<MetadataBuilder> builder = MetadataBuilder::New(
      {DefaultBlockDeviceInfo(size)}, LP_METADATA_DEFAULT_PARTITION_NAME,
      kMetadataMaxSize, kMetadataSlotCount);
  CF_EXPECT(builder.get());
  return builder;
}

Result<ComponentDisk> AddPartition(const std::string_view name,
                                   const std::string_view host_path,
                                   const std::string_view group_name,
                                   const uint32_t attributes,
                                   MetadataBuilder& metadata_builder) {
  std::unique_ptr<DiskImage> disk_image =
      CF_EXPECT(ImageFromFile(std::string(host_path)));
  const uint64_t partition_size = CF_EXPECT(disk_image->VirtualSizeBytes());

  CF_EXPECT_EQ(partition_size % LP_SECTOR_SIZE, 0);
  const uint64_t num_sectors = partition_size / LP_SECTOR_SIZE;

  std::optional<Interval> chosen_interval;
  for (const Interval& interval : metadata_builder.GetFreeRegions()) {
    if (interval.length() > num_sectors) {
      chosen_interval = interval;
      break;
    }
  }
  CF_EXPECT(chosen_interval.has_value());

  const LinearExtent extent(num_sectors, chosen_interval->device_index,
                            chosen_interval->start);

  Partition* partition_a = CF_EXPECT(metadata_builder.AddPartition(
      absl::StrCat(name, "_a"), absl::StrCat(group_name, "_a"), attributes));
  partition_a->AddExtent(std::make_unique<LinearExtent>(extent));
  partition_a->set_attributes(LP_PARTITION_ATTR_READONLY);

  Partition* partition_b = CF_EXPECT(metadata_builder.AddPartition(
      absl::StrCat(name, "_b"), absl::StrCat(group_name, "_b"), attributes));
  partition_b->set_attributes(LP_PARTITION_ATTR_READONLY);

  ComponentDisk component;
  component.set_file_path(host_path);
  component.set_offset(chosen_interval->start * LP_SECTOR_SIZE);
  component.set_read_write_capability(ReadWriteCapability::READ_WRITE);

  return component;
}

Result<uint64_t> ExpandedStorageSize(const std::string& file_path) {
  std::unique_ptr<DiskImage> disk = CF_EXPECT(ImageFromFile(file_path));
  CF_EXPECT(disk.get());
  return CF_EXPECT(disk->VirtualSizeBytes());
}

}  // namespace

CompositeSuperImageBuilder& CompositeSuperImageBuilder::BlockDeviceSize(
    const uint64_t size) & {
  size_ = size;
  return *this;
}

CompositeSuperImageBuilder CompositeSuperImageBuilder::BlockDeviceSize(
    const uint64_t size) && {
  BlockDeviceSize(size);
  return *this;
}

CompositeSuperImageBuilder& CompositeSuperImageBuilder::SystemPartition(
    const std::string_view name, const std::string_view host_path) & {
  system_partitions_.emplace(name, host_path);
  return *this;
}

CompositeSuperImageBuilder CompositeSuperImageBuilder::SystemPartition(
    const std::string_view name, const std::string_view host_path) && {
  SystemPartition(name, host_path);
  return *this;
}

CompositeSuperImageBuilder& CompositeSuperImageBuilder::VendorPartition(
    const std::string_view name, const std::string_view host_path) & {
  vendor_partitions_.emplace(name, host_path);
  return *this;
}

CompositeSuperImageBuilder CompositeSuperImageBuilder::VendorPartition(
    const std::string_view name, const std::string_view host_path) && {
  VendorPartition(name, host_path);
  return *this;
}

Result<std::string> CompositeSuperImageBuilder::WriteToDirectory(
    const std::string_view output_dir, const std::string_view file_name,
    const std::string_view header_name) {
  std::unique_ptr<MetadataBuilder> metadata_builder =
      CF_EXPECT(CreateMetadataBuilder(size_));

  static constexpr std::string_view kSystemGroup =
      "google_system_dynamic_partitions";
  static constexpr std::string_view kVendorGroup =
      "google_vendor_dynamic_partitions";

  CF_EXPECT(metadata_builder->AddGroup(absl::StrCat(kSystemGroup, "_a"), 0));
  CF_EXPECT(metadata_builder->AddGroup(absl::StrCat(kSystemGroup, "_b"), 0));

  CF_EXPECT(metadata_builder->AddGroup(absl::StrCat(kVendorGroup, "_a"), 0));
  CF_EXPECT(metadata_builder->AddGroup(absl::StrCat(kVendorGroup, "_b"), 0));

  std::vector<ComponentDisk> components;

  // TODO: attributes
  for (const auto& [name, path] : system_partitions_) {
    components.emplace_back(CF_EXPECT(
        AddPartition(name, path, kSystemGroup, 0, *metadata_builder)));
  }
  for (const auto& [name, path] : vendor_partitions_) {
    components.emplace_back(CF_EXPECT(
        AddPartition(name, path, kVendorGroup, 0, *metadata_builder)));
  }

  const std::unique_ptr<LpMetadata> metadata = metadata_builder->Export();
  CF_EXPECT(metadata.get());

  const std::string header_path = absl::StrCat(output_dir, "/", header_name);
  unlink(header_path.c_str());  // Ignore errors

  SharedFD header_fd =
      SharedFD::Open(header_path, O_RDWR | O_CREAT | O_EXCL, 0644);
  CF_EXPECT(header_fd->IsOpen(), header_fd->StrError());
  CF_EXPECT_EQ(header_fd->LSeek(LP_PARTITION_RESERVED_BYTES, SEEK_SET),
               LP_PARTITION_RESERVED_BYTES, header_fd->StrError());

  const std::string geometry_str = SerializeGeometry(metadata->geometry);
  const std::string metadata_str = SerializeMetadata(*metadata);

  CF_EXPECT(LogStringToDir(CuttlefishConfig::Get()->Instances()[0],
                           "generated_super.log", Pretty(metadata)));

  // We always use 2 slots, so 2 copies of metadata_str
  const std::string super_header =
      absl::StrCat(geometry_str, geometry_str, metadata_str, metadata_str);
  CF_EXPECT_EQ(WriteAll(header_fd, super_header), super_header.size(),
               header_fd->StrError());

  ComponentDisk& header_component = *components.emplace(components.begin());
  header_component.set_file_path(header_path);
  header_component.set_offset(0);
  header_component.set_read_write_capability(ReadWriteCapability::READ_WRITE);

  // TODO: schuffelen: fill the gaps using a single disk. the header is
  // relatively short and ends up being mapped thousands of times to fill the
  // gap. There needs to be something mapped to avoid producing disk errors on
  // access, and the dead space in the super image is being used to augment
  // userdata.
  for (auto it = components.begin(); it != components.end();) {
    const auto next = it + 1;
    const uint64_t part_size = CF_EXPECT(ExpandedStorageSize(it->file_path()));
    const uint64_t end = it->offset() + part_size;
    const uint64_t next_offset =
        next == components.end() ? size_ : next->offset();
    if (end < next_offset) {
      it = components.emplace(next);
      ComponentDisk& fill_hole = *it;
      fill_hole.set_file_path(header_path);
      fill_hole.set_offset(end);
      fill_hole.set_read_write_capability(ReadWriteCapability::READ_WRITE);
    } else {
      it++;
    }
  }

  const std::string file_path = absl::StrCat(output_dir, "/", file_name);
  unlink(file_path.c_str());  // Ignore errors

  CompositeDisk composite;
  composite.set_version(2);
  composite.set_length(size_);
  for (const ComponentDisk& component : components) {
    *composite.add_component_disks() = component;
  }

  SharedFD composite_fd =
      SharedFD::Open(file_path, O_RDWR | O_CREAT | O_EXCL, 0644);
  CF_EXPECT(composite_fd->IsOpen(), composite_fd->StrError());

  const std::string magic = CompositeDiskImage::MagicString();
  CF_EXPECT_EQ(WriteAll(composite_fd, magic), magic.size(),
               composite_fd->StrError());

  const std::string composite_string = composite.SerializeAsString();
  CF_EXPECT_EQ(WriteAll(composite_fd, composite_string),
               composite_string.size(), composite_fd->StrError());

  return file_path;
}

}  // namespace cuttlefish
