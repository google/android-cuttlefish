/*
 * Copyright (C) 2019 The Android Open Source Project
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
#pragma once

#include <cstdint>

#include "cuttlefish/common/libs/utils/size_utils.h"
#include "cuttlefish/host/libs/image_aggregator/mbr.h"

namespace cuttlefish {

constexpr int GPT_NUM_PARTITIONS = 128;

struct __attribute__((packed)) GptHeader {
  std::uint8_t signature[8];
  std::uint8_t revision[4];
  std::uint32_t header_size;
  std::uint32_t header_crc32;
  std::uint32_t reserved;
  std::uint64_t current_lba;
  std::uint64_t backup_lba;
  std::uint64_t first_usable_lba;
  std::uint64_t last_usable_lba;
  std::uint8_t disk_guid[16];
  std::uint64_t partition_entries_lba;
  std::uint32_t num_partition_entries;
  std::uint32_t partition_entry_size;
  std::uint32_t partition_entries_crc32;
};

static_assert(sizeof(GptHeader) == 92);

struct __attribute__((packed)) GptPartitionEntry {
  std::uint8_t partition_type_guid[16];
  std::uint8_t unique_partition_guid[16];
  std::uint64_t first_lba;
  std::uint64_t last_lba;
  std::uint64_t attributes;
  std::uint16_t partition_name[36];  // UTF-16LE
};

static_assert(sizeof(GptPartitionEntry) == 128);

struct __attribute__((packed)) GptBeginning {
  MasterBootRecord protective_mbr;
  GptHeader header;
  std::uint8_t header_padding[kSectorSize - sizeof(GptHeader)];
  GptPartitionEntry entries[GPT_NUM_PARTITIONS];
  std::uint8_t partition_alignment[3072];
};

static_assert(AlignToPowerOf2(sizeof(GptBeginning), PARTITION_SIZE_SHIFT) ==
              sizeof(GptBeginning));

struct __attribute__((packed)) GptEnd {
  GptPartitionEntry entries[GPT_NUM_PARTITIONS];
  GptHeader footer;
  std::uint8_t footer_padding[kSectorSize - sizeof(GptHeader)];
};

static_assert(sizeof(GptEnd) % kSectorSize == 0);

}  // namespace cuttlefish
