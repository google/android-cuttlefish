//
// Copyright (C) 2026 The Android Open Source Project
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

#include "cuttlefish/pretty/liblp/metadata_format.h"

#include "liblp/liblp.h"
#include "liblp/metadata_format.h"

#include "cuttlefish/pretty/pretty.h"
#include "cuttlefish/pretty/string.h"
#include "cuttlefish/pretty/struct.h"
#include "cuttlefish/pretty/vector.h"

namespace cuttlefish {
namespace {

// Use this when accessing packed struct fields as they may be unaligned.
// PrettyStruct::Member takes arguments by reference, and creating a reference
// to an unaligned field violates the standard.
template <typename T>
T ByValue(const T value) {
  return value;
}

}  // namespace

PrettyStruct Pretty(const LpMetadataGeometry& geometry, PrettyAdlPlaceholder) {
  std::vector<uint8_t> checksum(
      geometry.checksum,
      geometry.checksum + (sizeof(geometry.checksum) / sizeof(uint8_t)));
  return PrettyStruct("LpMetadataGeometry")
      .Member("magic", ByValue(geometry.magic))
      .Member("struct_size", ByValue(geometry.struct_size))
      .Member("checksum", checksum)
      .Member("metadata_max_size", ByValue(geometry.metadata_max_size))
      .Member("metadata_slot_count", ByValue(geometry.metadata_slot_count))
      .Member("logical_block_size", ByValue(geometry.logical_block_size));
}

PrettyStruct Pretty(const LpMetadataTableDescriptor& metadata_table_descriptor,
                    PrettyAdlPlaceholder) {
  return PrettyStruct("LpMetadataTableDescriptor")
      .Member("offset", ByValue(metadata_table_descriptor.offset))
      .Member("num_entries", ByValue(metadata_table_descriptor.num_entries))
      .Member("entry_size", ByValue(metadata_table_descriptor.entry_size));
}

PrettyStruct Pretty(const LpMetadataHeader& metadata_header,
                    PrettyAdlPlaceholder) {
  std::vector<uint8_t> header_checksum(
      metadata_header.header_checksum,
      metadata_header.header_checksum +
          (sizeof(metadata_header.header_checksum) / sizeof(uint8_t)));
  std::vector<uint8_t> tables_checksum(
      metadata_header.tables_checksum,
      metadata_header.tables_checksum +
          (sizeof(metadata_header.tables_checksum) / sizeof(uint8_t)));
  return PrettyStruct("LpMetadataHeader")
      .Member("magic", ByValue(metadata_header.magic))
      .Member("major_version", ByValue(metadata_header.major_version))
      .Member("minor_version", ByValue(metadata_header.minor_version))
      .Member("header_size", ByValue(metadata_header.header_size))
      .Member("header_checksum", header_checksum)
      .Member("tables_size", ByValue(metadata_header.tables_size))
      .Member("tables_checksum", tables_checksum)
      .Member("partitions", ByValue(metadata_header.partitions))
      .Member("extents", ByValue(metadata_header.extents))
      .Member("groups", ByValue(metadata_header.groups))
      .Member("block_devices", ByValue(metadata_header.block_devices))
      .Member("flags", ByValue(metadata_header.flags));
}

PrettyStruct Pretty(const LpMetadataPartition& metadata_partition,
                    PrettyAdlPlaceholder) {
  return PrettyStruct("LpMetadataPartition")
      .Member("name", android::fs_mgr::GetPartitionName(metadata_partition))
      .Member("attributes", ByValue(metadata_partition.attributes))
      .Member("first_extent_index",
              ByValue(metadata_partition.first_extent_index))
      .Member("num_extents", ByValue(metadata_partition.num_extents))
      .Member("group_index", ByValue(metadata_partition.group_index));
}

PrettyStruct Pretty(const LpMetadataExtent& metadata_extent,
                    PrettyAdlPlaceholder) {
  return PrettyStruct("LpMetadataExtent")
      .Member("num_sectors", ByValue(metadata_extent.num_sectors))
      .Member("target_type", ByValue(metadata_extent.target_type))
      .Member("target_data", ByValue(metadata_extent.target_data))
      .Member("target_source", ByValue(metadata_extent.target_source));
}

PrettyStruct Pretty(const LpMetadataPartitionGroup& metadata_partition_group,
                    PrettyAdlPlaceholder) {
  return PrettyStruct("LpMetadataPartitionGroup")
      .Member("name",
              android::fs_mgr::GetPartitionGroupName(metadata_partition_group))
      .Member("flags", ByValue(metadata_partition_group.flags))
      .Member("maximum_size", ByValue(metadata_partition_group.maximum_size));
}

PrettyStruct Pretty(const LpMetadataBlockDevice& block_device,
                    PrettyAdlPlaceholder) {
  std::string_view partition_name(
      block_device.partition_name,
      sizeof(block_device.partition_name) / sizeof(char));
  return PrettyStruct("LpMetadataBlockDevice")
      .Member("first_logical_sector",
              ByValue(block_device.first_logical_sector))
      .Member("alignment", ByValue(block_device.alignment))
      .Member("alignment_offset", ByValue(block_device.alignment_offset))
      .Member("size", ByValue(block_device.size))
      .Member("partition_name", ByValue(partition_name))
      .Member("flags", ByValue(block_device.flags));
}

}  // namespace cuttlefish
