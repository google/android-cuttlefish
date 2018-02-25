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

#include <string.h>

#include <cutils/log.h>
#include <hardware/gralloc.h>

#include "common/vsoc/lib/screen_region_view.h"
#include "guest/hals/gralloc/legacy/gralloc_vsoc_priv.h"

using vsoc::screen::ScreenRegionView;

namespace cvd {

namespace {

void BroadcastFrameBufferChanged(int index) {
  ScreenRegionView::GetInstance()->BroadcastNewFrame(
      static_cast<uint32_t>(index));
}

}  // namespace

BaseComposer::BaseComposer(int64_t vsync_base_timestamp,
                           int32_t vsync_period_ns)
    : vsync_base_timestamp_(vsync_base_timestamp),
      vsync_period_ns_(vsync_period_ns),
      fb_broadcaster_(BroadcastFrameBufferChanged) {
  hw_get_module(GRALLOC_HARDWARE_MODULE_ID,
                reinterpret_cast<const hw_module_t**>(&gralloc_module_));
}

BaseComposer::~BaseComposer() {}

FbBroadcaster BaseComposer::ReplaceFbBroadcaster(FbBroadcaster fb_broadcaster) {
  FbBroadcaster tmp = fb_broadcaster_;
  fb_broadcaster_ = fb_broadcaster;
  return tmp;
}

void BaseComposer::Dump(char* buff __unused, int buff_len __unused) {}

void BaseComposer::Broadcast(int fb_index) {
  fb_broadcaster_(fb_index);
}

int BaseComposer::PostFrameBufferTarget(buffer_handle_t buffer_handle) {
  int fb_index = NextScreenBuffer();
  auto screen_view = ScreenRegionView::GetInstance();
  void* frame_buffer = screen_view->GetBuffer(fb_index);
  const private_handle_t* p_handle =
      reinterpret_cast<const private_handle_t*>(buffer_handle);
  void* buffer;
  int retval = gralloc_module_->lock(gralloc_module_, buffer_handle,
                                     GRALLOC_USAGE_SW_READ_OFTEN, 0, 0,
                                     p_handle->x_res, p_handle->y_res, &buffer);
  if (retval != 0) {
    ALOGE("Got error code %d from lock function", retval);
    return -1;
  }
  memcpy(frame_buffer, buffer, screen_view->buffer_size());
  Broadcast(fb_index);
  return 0;
}  // namespace cvd

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

int BaseComposer::SetLayers(size_t num_layers, vsoc_hwc_layer* layers) {
  for (size_t idx = 0; idx < num_layers; idx++) {
    if (IS_TARGET_FRAMEBUFFER(layers[idx].compositionType)) {
      return PostFrameBufferTarget(layers[idx].handle);
    }
  }
  return -1;
}

int BaseComposer::NextScreenBuffer() {
  last_frame_buffer_ = (last_frame_buffer_ + 1) %
                       ScreenRegionView::GetInstance()->number_of_buffers();
  return last_frame_buffer_;
}

}  // namespace cvd
