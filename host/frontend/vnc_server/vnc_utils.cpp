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

#include "common/vsoc/lib/fb_bcast_region_view.h"
#include "host/libs/config/host_config.h"

using vsoc::framebuffer::FBBroadcastRegionView;

namespace cvd {
namespace vnc {

std::shared_ptr<FBBroadcastRegionView> GetFBBroadcastRegionView() {
  std::shared_ptr<FBBroadcastRegionView> region =
      FBBroadcastRegionView::GetInstance(vsoc::GetDomain().c_str());
  if (!region) {
    LOG(FATAL) << "Unable to open FBBroadcastRegion";
  }
  return region;
}

}  // namespace vnc
}  // namespace cvd
