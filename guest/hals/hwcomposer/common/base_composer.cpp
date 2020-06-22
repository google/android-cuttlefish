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
#include <log/log.h>

namespace cuttlefish {

BaseComposer::BaseComposer(std::unique_ptr<ScreenView> screen_view)
    : screen_view_(std::move(screen_view)), gralloc_() {}

void BaseComposer::Dump(char* buff __unused, int buff_len __unused) {}

int BaseComposer::PostFrameBufferTarget(buffer_handle_t buffer_handle) {
  auto buffer_id = screen_view_->NextBuffer();
  void* frame_buffer = screen_view_->GetBuffer(buffer_id);

  auto imported_buffer_opt = gralloc_.Import(buffer_handle);
  if (!imported_buffer_opt) {
    ALOGE("Failed to Import() framebuffer for post.");
    return -1;
  }
  GrallocBuffer& imported_buffer = *imported_buffer_opt;

  auto buffer_opt = imported_buffer.Lock();
  if (!buffer_opt) {
    ALOGE("Failed to Lock() framebuffer for post.");
    return -1;
  }

  void* buffer = *buffer_opt;
  memcpy(frame_buffer, buffer, screen_view_->buffer_size());

  imported_buffer.Unlock();

  screen_view_->Broadcast(buffer_id);
  return 0;
}  // namespace cuttlefish

bool BaseComposer::IsValidLayer(const hwc_layer_1_t& layer) {
  auto buffer_opt = gralloc_.Import(layer.handle);
  if (!buffer_opt) {
    ALOGE("Failed to import and validate layer buffer handle.");
    return false;
  }
  GrallocBuffer& buffer = *buffer_opt;

  auto buffer_width_opt = buffer.GetWidth();
  if (!buffer_width_opt) {
    ALOGE("Failed to get layer buffer width.");
    return false;
  }
  uint32_t buffer_width = *buffer_width_opt;

  auto buffer_height_opt = buffer.GetHeight();
  if (!buffer_height_opt) {
    ALOGE("Failed to get layer buffer height.");
    return false;
  }
  uint32_t buffer_height = *buffer_height_opt;

  if (layer.sourceCrop.left < 0 || layer.sourceCrop.top < 0 ||
      layer.sourceCrop.right > buffer_width ||
      layer.sourceCrop.bottom > buffer_height) {
    ALOGE(
        "%s: Invalid sourceCrop for buffer handle: sourceCrop = [left = %d, "
        "right = %d, top = %d, bottom = %d], handle = [width = %d, height = "
        "%d]",
        __FUNCTION__, layer.sourceCrop.left, layer.sourceCrop.right,
        layer.sourceCrop.top, layer.sourceCrop.bottom, buffer_width,
        buffer_height);
    return false;
  }
  return true;
}

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
