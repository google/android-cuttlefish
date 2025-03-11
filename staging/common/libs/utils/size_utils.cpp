/*
 * Copyright (C) 2018 The Android Open Source Project
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

#include "common/libs/utils/size_utils.h"

#include <unistd.h>

namespace cvd {

uint32_t AlignToPageSize(uint32_t val) {
  static uint32_t page_size = sysconf(_SC_PAGESIZE);
  return ((val + (page_size - 1)) / page_size) * page_size;
}

uint32_t RoundUpToNextPowerOf2(uint32_t val) {
  uint32_t power_of_2 = 1;
  while (power_of_2 < val) {
    power_of_2 *= 2;
  }
  return power_of_2;
}

uint32_t AlignToPowerOf2(uint32_t val, uint8_t align_log) {
  uint32_t align = 1 << align_log;
  return ((val + (align - 1)) / align) * align;
}

}  // namespace cvd
