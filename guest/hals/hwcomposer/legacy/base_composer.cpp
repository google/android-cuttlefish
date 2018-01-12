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
#include "guest/hals/gralloc//legacy/gralloc_vsoc_priv.h"
#include "guest/libs/legacy_framebuffer/vsoc_framebuffer_control.h"

namespace cvd {

namespace {

int BroadcastFrameBufferChanged(int yoffset) {
  return VSoCFrameBufferControl::getInstance().BroadcastFrameBufferChanged(
      yoffset);
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

int BaseComposer::PostFrameBuffer(buffer_handle_t buffer) {
  const int yoffset = YOffsetFromHandle(buffer);
  // If the broadcaster is NULL or could not get a good yoffset just ignore it.
  if (fb_broadcaster_ && yoffset >= 0) {
    int retval = fb_broadcaster_(yoffset);
    if (retval) {
      ALOGI("Failed to post framebuffer");
      return -1;
    }
  }

  return yoffset;
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

int BaseComposer::SetLayers(size_t num_layers, vsoc_hwc_layer* layers) {
  for (size_t idx = 0; idx < num_layers; idx++) {
    if (IS_TARGET_FRAMEBUFFER(layers[idx].compositionType)) {
      return PostFrameBuffer(layers[idx].handle);
    }
  }
  return -1;
}

}  // namespace cvd
