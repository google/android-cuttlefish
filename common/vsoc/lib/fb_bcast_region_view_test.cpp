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
#include "fb_bcast_region_view.h"
#include "host/libs/config/host_config.h"
#include <stdio.h>

using vsoc::framebuffer::FBBroadcastRegionView;

int main() {
  uint32_t frame_num = 0;
  vsoc_reg_off_t offset = 0;

#if defined(CUTTLEFISH_HOST)
  auto region = FBBroadcastRegionView::GetInstance(vsoc::GetDomain().c_str());
#else
  auto region = FBBroadcastRegionView::GetInstance();
#endif
  if (!region) {
    fprintf(stderr, "Error opening region\n");
    return 1;
  }

  while (1) {
    offset = region->WaitForNewFrameSince(&frame_num);
    printf("Signaled frame_num = %d, offset = 0x%x\n", frame_num, offset);
  }

  return 0;
}
