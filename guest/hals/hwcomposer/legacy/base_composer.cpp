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

#include "base_composer.h"

#include <cutils/log.h>

#include "common/vsoc/lib/fb_bcast_region_view.h"
#include "guest/hals/gralloc/legacy/gralloc_vsoc_priv.h"

using vsoc::framebuffer::FBBroadcastRegionView;

namespace cvd {

namespace {

void BroadcastFrameBufferChanged(int32_t offset) {
  FBBroadcastRegionView::GetInstance()->BroadcastNewFrame(
      static_cast<uint32_t>(offset));
}

}  // namespace

BaseComposer::BaseComposer(int64_t vsync_base_timestamp,
                           int32_t vsync_period_ns)
    : vsync_base_timestamp_(vsync_base_timestamp),
      vsync_period_ns_(vsync_period_ns),
      fb_broadcaster_(BroadcastFrameBufferChanged) {
  const gralloc_module_t* gralloc_module;
  hw_get_module(GRALLOC_HARDWARE_MODULE_ID,
                reinterpret_cast<const hw_module_t**>(&gralloc_module));
}

BaseComposer::~BaseComposer() {}

FbBroadcaster BaseComposer::ReplaceFbBroadcaster(FbBroadcaster fb_broadcaster) {
  FbBroadcaster tmp = fb_broadcaster_;
  fb_broadcaster_ = fb_broadcaster;
  return tmp;
}

void BaseComposer::Dump(char* buff __unused, int buff_len __unused) {}

int32_t BaseComposer::PostFrameBuffer(buffer_handle_t buffer) {
  const int32_t offset = OffsetFromHandle(buffer);
  // If the broadcaster is NULL or could not get a good offset just ignore it.
  if (fb_broadcaster_ && offset >= 0) {
    fb_broadcaster_(offset);
  }
  return offset;
}

int BaseComposer::PrepareLayers(size_t num_layers, vsoc_hwc_layer* layers) {
  // find unsupported overlays
  for (size_t i = 0; i < num_layers; i++) {
    if (IS_TARGET_FRAMEBUFFER(layers[i].compositionType)) {
      continue;
    }
    layers[i].compositionType = HWC_FRAMEBUFFER;
  }
  return 0;
}

int32_t BaseComposer::SetLayers(size_t num_layers, vsoc_hwc_layer* layers) {
  for (size_t idx = 0; idx < num_layers; idx++) {
    if (IS_TARGET_FRAMEBUFFER(layers[idx].compositionType)) {
      return PostFrameBuffer(layers[idx].handle);
    }
  }
  return -1;
}

}  // namespace cvd
