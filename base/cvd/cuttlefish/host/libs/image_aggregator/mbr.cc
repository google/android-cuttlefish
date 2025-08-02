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
#include "cuttlefish/host/libs/image_aggregator/mbr.h"

#include <stdint.h>

namespace cuttlefish {

MasterBootRecord ProtectiveMbr(uint64_t size) {
  MasterBootRecord mbr = {
      .partitions = {{
          .partition_type = 0xEE,
          .first_lba = 1,
          .num_sectors = (uint32_t)size / kSectorSize,
      }},
      .boot_signature = {0x55, 0xAA},
  };
  return mbr;
}

}  // namespace cuttlefish
