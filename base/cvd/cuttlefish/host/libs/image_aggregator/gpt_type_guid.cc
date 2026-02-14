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
#include "cuttlefish/host/libs/image_aggregator/gpt_type_guid.h"

#include <stdint.h>

#include "cuttlefish/result/result.h"

namespace cuttlefish {

Result<const uint8_t*> GetPartitionGUID(GptPartitionType type) {
  // Due to some endianness mismatch in e2fsprogs GUID vs GPT, the GUIDs are
  // rearranged to make the right GUIDs appear in gdisk
  switch (type) {
    case GptPartitionType::kLinuxFilesystem: {
      static constexpr uint8_t kLinuxFileSystemGuid[] = {
          0xaf, 0x3d, 0xc6, 0xf,  0x83, 0x84, 0x72, 0x47,
          0x8e, 0x79, 0x3d, 0x69, 0xd8, 0x47, 0x7d, 0xe4};
      return kLinuxFileSystemGuid;
    }
    case GptPartitionType::kEfiSystemPartition:
      static constexpr uint8_t kEfiSystemPartitionGuid[] = {
          0x28, 0x73, 0x2a, 0xc1, 0x1f, 0xf8, 0xd2, 0x11,
          0xba, 0x4b, 0x0,  0xa0, 0xc9, 0x3e, 0xc9, 0x3b};
      return kEfiSystemPartitionGuid;
    default:
      return CF_ERRF("Unknown partition type: {}", (int)type);
  }
}

}  // namespace cuttlefish
