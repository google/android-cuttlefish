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

#include <glog/logging.h>

#include "common/vsoc/lib/screen_region_view.h"
#include "host/commands/launch/pre_launch_initializers.h"
#include "host/libs/config/cuttlefish_config.h"

void InitializeScreenRegion(const vsoc::CuttlefishConfig& config) {
  auto region =
      vsoc::screen::ScreenRegionView::GetInstance(vsoc::GetDomain().c_str());
  if (!region) {
    LOG(FATAL) << "Screen region was not found";
    return;
  }
  auto dest = region->data();
  dest->x_res = config.x_res();
  dest->y_res = config.y_res();
  dest->dpi = config.dpi();
  dest->refresh_rate_hz = config.refresh_rate_hz();
}
