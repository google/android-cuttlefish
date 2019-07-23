/*
 * Copyright (C) 2016 The Android Open Source Project
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

#include "guest/hals/hwcomposer/vsoc/vsoc_screen_view.h"

#include <sys/time.h>

#include "common/vsoc/lib/screen_region_view.h"

using vsoc::layout::screen::TimeSpec;
using vsoc::screen::ScreenRegionView;

namespace cvd {
namespace {

TimeSpec TimeSpecFromSystemStruct(const timespec* spec) {
  return {static_cast<uint32_t>(spec->tv_sec),
          static_cast<uint32_t>(spec->tv_nsec), 0};
}

void VSoCStatsFromCvdStats(vsoc::layout::screen::CompositionStats* vsoc_stats,
                           const cvd::CompositionStats* stats) {
  vsoc_stats->num_prepare_calls = stats->num_prepare_calls;
  vsoc_stats->num_layers = stats->num_layers;
  vsoc_stats->num_hwcomposited_layers = stats->num_hwcomposited_layers;
  vsoc_stats->last_vsync = TimeSpecFromSystemStruct(&stats->last_vsync);
  vsoc_stats->prepare_start = TimeSpecFromSystemStruct(&stats->prepare_start);
  vsoc_stats->prepare_end = TimeSpecFromSystemStruct(&stats->prepare_end);
  vsoc_stats->set_start = TimeSpecFromSystemStruct(&stats->set_start);
  vsoc_stats->set_end = TimeSpecFromSystemStruct(&stats->set_end);
}

}  // namespace

VSoCScreenView::VSoCScreenView()
    : region_view_(ScreenRegionView::GetInstance()) {}

VSoCScreenView::~VSoCScreenView() {}

void VSoCScreenView::Broadcast(int buffer_id,
                               const cvd::CompositionStats* stats) {
  if (stats) {
    vsoc::layout::screen::CompositionStats vsoc_stats;
    VSoCStatsFromCvdStats(&vsoc_stats, stats);
    region_view_->BroadcastNewFrame(buffer_id, &vsoc_stats);
  } else {
    region_view_->BroadcastNewFrame(buffer_id);
  }
}

void* VSoCScreenView::GetBuffer(int fb_index) {
  return region_view_->GetBuffer(fb_index);
}

int32_t VSoCScreenView::x_res() const { return region_view_->x_res(); }

int32_t VSoCScreenView::y_res() const { return region_view_->y_res(); }

int32_t VSoCScreenView::dpi() const { return region_view_->dpi(); }

int32_t VSoCScreenView::refresh_rate() const {
  return region_view_->refresh_rate_hz();
}

int VSoCScreenView::num_buffers() const {
  return region_view_->number_of_buffers();
}
}  // namespace cvd
