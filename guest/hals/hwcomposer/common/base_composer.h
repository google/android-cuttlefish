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

#pragma once

#include <memory>

#include "guest/hals/hwcomposer/common/gralloc_utils.h"
#include "guest/hals/hwcomposer/common/hwcomposer.h"
#include "guest/hals/hwcomposer/common/screen_view.h"

namespace cuttlefish {

class BaseComposer {
 public:
  BaseComposer(std::unique_ptr<ScreenView> screen_view);
  virtual ~BaseComposer() = default;

  virtual bool IsValidLayer(const hwc_layer_1_t& layer);
  // Sets the composition type of each layer and returns the number of layers
  // to be composited by the hwcomposer.
  virtual int PrepareLayers(size_t num_layers, hwc_layer_1_t* layers);
  // Returns 0 if successful.
  virtual int SetLayers(size_t num_layers, hwc_layer_1_t* layers);
  virtual void Dump(char* buff, int buff_len);

  int32_t x_res() { return screen_view_->x_res(); }
  int32_t y_res() { return screen_view_->y_res(); }
  int32_t dpi() { return screen_view_->dpi(); }
  int32_t refresh_rate() { return screen_view_->refresh_rate(); }

 protected:
  std::unique_ptr<ScreenView> screen_view_;
  Gralloc gralloc_;

 private:
  // Returns buffer offset or negative on error.
  int PostFrameBufferTarget(buffer_handle_t handle);
};
}  // namespace cuttlefish
