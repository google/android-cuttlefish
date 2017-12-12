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

#ifndef GCE_HWCOMPOSER_GCE_COMPOSER_H
#define GCE_HWCOMPOSER_GCE_COMPOSER_H

#include <hardware/gralloc.h>
#include "gralloc_gce_priv.h"
#include "hwcomposer_common.h"
#include "base_composer.h"

#include <vector>

namespace cvd {

class GceComposer : public BaseComposer {
 public:
  GceComposer(int64_t vsync_base_timestamp, int32_t vsync_period_ns);
  ~GceComposer();

  // override
  int PrepareLayers(size_t num_layers, gce_hwc_layer* layers);
  // override
  int SetLayers(size_t num_layers, gce_hwc_layer* layers);

 protected:
  static const int kNumTmpBufferPieces;
  uint8_t* RotateTmpBuffer(unsigned int order);
  uint8_t* GetSpecialTmpBuffer(size_t needed_size);
  buffer_handle_t FindFrameBuffer(int num_layers, gce_hwc_layer* layers);
  void CompositeLayer(gce_hwc_layer* src_layer, buffer_handle_t dst_layer);
  std::vector<uint8_t> tmp_buffer_;
  std::vector<uint8_t> special_tmp_buffer_;
  const gralloc_module_t* gralloc_module_;
  priv_alloc_device_t* gralloc_dev_;
  std::vector<buffer_handle_t> hwc_framebuffers_;
  int next_hwc_framebuffer_;
};

}  // namespace cvd

#endif
