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

constexpr int SECTOR_SIZE = 512;

struct __attribute__((packed)) MbrPartitionEntry {
  std::uint8_t status;
  std::uint8_t begin_chs[3];
  std::uint8_t partition_type;
  std::uint8_t end_chs[3];
  std::uint32_t first_lba;
  std::uint32_t num_sectors;
};

struct __attribute__((packed)) MasterBootRecord {
  std::uint8_t bootstrap_code[446];
  MbrPartitionEntry partitions[4];
  std::uint8_t boot_signature[2];
};

static_assert(sizeof(MasterBootRecord) == SECTOR_SIZE);
