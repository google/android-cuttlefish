#pragma once
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

#include <vector>

#include <hardware/gralloc.h>

#include "guest/hals/gralloc/legacy/gralloc_vsoc_priv.h"

#include "base_composer.h"
#include "hwcomposer_common.h"

namespace cvd {

class VSoCComposer : public BaseComposer {
 public:
  VSoCComposer(int64_t vsync_base_timestamp, int32_t vsync_period_ns);
  ~VSoCComposer();

  // override
  int PrepareLayers(size_t num_layers, vsoc_hwc_layer* layers);
  // override
  int SetLayers(size_t num_layers, vsoc_hwc_layer* layers);

 protected:
  static const int kNumTmpBufferPieces;
  uint8_t* RotateTmpBuffer(unsigned int order);
  uint8_t* GetSpecialTmpBuffer(size_t needed_size);
  void CompositeLayer(vsoc_hwc_layer* src_layer, int32_t fb_offset);
  std::vector<uint8_t> tmp_buffer_;
  std::vector<uint8_t> special_tmp_buffer_;
};

}  // namespace cvd
