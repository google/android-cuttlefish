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
#pragma once

#include <stdint.h>

namespace cuttlefish {

inline constexpr int kSectorSizeShift = 9;
inline constexpr int kSectorSize = 1 << kSectorSizeShift;

struct __attribute__((packed)) MbrPartitionEntry {
  uint8_t status;
  uint8_t begin_chs[3];
  uint8_t partition_type;
  uint8_t end_chs[3];
  uint32_t first_lba;
  uint32_t num_sectors;
};

struct __attribute__((packed)) MasterBootRecord {
  uint8_t bootstrap_code[446];
  MbrPartitionEntry partitions[4];
  uint8_t boot_signature[2];
};

static_assert(sizeof(MasterBootRecord) == kSectorSize);

/**
 * Creates a "Protective" MBR Partition Table header. The GUID
 * Partition Table Specification recommends putting this on the first sector
 * of the disk, to protect against old disk formatting tools from misidentifying
 * the GUID Partition Table later and doing the wrong thing.
 */
MasterBootRecord ProtectiveMbr(uint64_t size);

}  // namespace cuttlefish
