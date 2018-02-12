#pragma once
/*
 * Copyright (C) 2017 The Android Open Source Project
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

#include "common/vsoc/shm/base.h"
#include "common/vsoc/shm/version.h"

// Memory layout for the ril hal region

namespace vsoc {
namespace layout {
namespace ril {

struct RilLayout : public RegionLayout {
  static const char* region_name;

  char ipaddr[16]; // xxx.xxx.xxx.xxx\0 = 16 bytes
  char gateway[16];
  char dns[16];
  char broadcast[16];
  uint32_t prefixlen;
};
ASSERT_SHM_COMPATIBLE(RilLayout, ril);
}  // namespace ril
}  // namespace layout
}  // namespace vsoc
