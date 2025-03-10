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
#pragma once

#include <stdint.h>

namespace cvd {

// Returns the smallest multiple of PAGE_SIZE greater than or equal to val.
uint32_t AlignToPageSize(uint32_t val);

// Returns the smallest power of two greater than or equal to val.
uint32_t RoundUpToNextPowerOf2(uint32_t val);

// Returns the smallest multiple of 2^align_log greater than or equal to val.
uint32_t AlignToPowerOf2(uint32_t val, uint8_t align_log);

}  // namespace cvd
