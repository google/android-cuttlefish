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

#include <stdint.h>

#include "cuttlefish/common/libs/utils/size_utils.h"
#include "cuttlefish/host/libs/image_aggregator/mbr.h"

namespace cuttlefish {

constexpr int GPT_NUM_PARTITIONS = 128;

struct __attribute__((packed)) GptHeader {
  uint8_t signature[8];
  uint8_t revision[4];
  uint32_t header_size;
  uint32_t header_crc32;
  uint32_t reserved;
  uint64_t current_lba;
  uint64_t backup_lba;
  uint64_t first_usable_lba;
  uint64_t last_usable_lba;
  uint8_t disk_guid[16];
  uint64_t partition_entries_lba;
  uint32_t num_partition_entries;
  uint32_t partition_entry_size;
  uint32_t partition_entries_crc32;
};

static_assert(sizeof(GptHeader) == 92);

struct __attribute__((packed)) GptPartitionEntry {
  uint8_t partition_type_guid[16];
  uint8_t unique_partition_guid[16];
  uint64_t first_lba;
  uint64_t last_lba;
  uint64_t attributes;
  uint16_t partition_name[36];  // UTF-16LE
};

static_assert(sizeof(GptPartitionEntry) == 128);

struct __attribute__((packed)) GptBeginning {
  MasterBootRecord protective_mbr;
  GptHeader header;
  uint8_t header_padding[kSectorSize - sizeof(GptHeader)];
  GptPartitionEntry entries[GPT_NUM_PARTITIONS];
  uint8_t partition_alignment[3072];
};

static_assert(AlignToPowerOf2(sizeof(GptBeginning), PARTITION_SIZE_SHIFT) ==
              sizeof(GptBeginning));

struct __attribute__((packed)) GptEnd {
  GptPartitionEntry entries[GPT_NUM_PARTITIONS];
  GptHeader footer;
  uint8_t footer_padding[kSectorSize - sizeof(GptHeader)];
};

static_assert(sizeof(GptEnd) % kSectorSize == 0);

}  // namespace cuttlefish
