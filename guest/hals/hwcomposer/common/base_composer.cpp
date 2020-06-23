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

#include "guest/hals/hwcomposer/common/base_composer.h"

#include <string.h>

#include <cutils/properties.h>
#include <hardware/gralloc.h>
#include <log/log.h>

#include "guest/hals/gralloc/legacy/gralloc_vsoc_priv.h"

namespace cuttlefish {

BaseComposer::BaseComposer(std::unique_ptr<ScreenView> screen_view)
    : screen_view_(std::move(screen_view)) {
  hw_get_module(GRALLOC_HARDWARE_MODULE_ID,
                reinterpret_cast<const hw_module_t**>(&gralloc_module_));
}

void BaseComposer::Dump(char* buff __unused, int buff_len __unused) {}

int BaseComposer::PostFrameBufferTarget(buffer_handle_t buffer_handle) {
  auto buffer_id = screen_view_->NextBuffer();
  void* frame_buffer = screen_view_->GetBuffer(buffer_id);
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
  memcpy(frame_buffer, buffer, screen_view_->buffer_size());
  screen_view_->Broadcast(buffer_id);
  return 0;
}  // namespace cuttlefish

int BaseComposer::PrepareLayers(size_t num_layers, hwc_layer_1_t* layers) {
  // find unsupported overlays
  for (size_t i = 0; i < num_layers; i++) {
    if (IS_TARGET_FRAMEBUFFER(layers[i].compositionType)) {
      continue;
    }
    layers[i].compositionType = HWC_FRAMEBUFFER;
  }
  return 0;
}

int BaseComposer::SetLayers(size_t num_layers, hwc_layer_1_t* layers) {
  for (size_t idx = 0; idx < num_layers; idx++) {
    if (IS_TARGET_FRAMEBUFFER(layers[idx].compositionType)) {
      return PostFrameBufferTarget(layers[idx].handle);
    }
  }
  return -1;
}

}  // namespace cuttlefish
