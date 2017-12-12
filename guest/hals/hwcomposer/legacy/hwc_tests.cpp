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
#include "vsoc_composer.h"
#include <stdio.h>

// This executable is only intended to perform simple tests on the hwcomposer
// functionality. It should not be part of the images, but rather be included
// (via scp) when needed to test specific scenarios that are hard to reproduce
// in the normal operation of the device.

class HWC_Tester : public cvd::VSoCComposer {
 public:
  HWC_Tester() : cvd::VSoCComposer(int64_t(0), int32_t(16000000)) {}
  int RunTest() {
    // Allocate two buffers (1x1 and 800x1280)
    buffer_handle_t src_handle;
    int src_stride;
    int res = gralloc_dev_->device.alloc(&gralloc_dev_->device,
                                         1,
                                         1,
                                         HAL_PIXEL_FORMAT_RGBA_8888,
                                         GRALLOC_USAGE_SW_READ_OFTEN,
                                         &src_handle,
                                         &src_stride);
    if (res) {
      fprintf(stderr, "Error allocating source buffer, see logs for details\n");
      return -1;
    }
    buffer_handle_t dst_handle;
    int dst_stride;
    res = gralloc_dev_->device.alloc(&gralloc_dev_->device,
                                     800,
                                     1280,
                                     HAL_PIXEL_FORMAT_RGBA_8888,
                                     GRALLOC_USAGE_SW_WRITE_OFTEN,
                                     &dst_handle,
                                     &dst_stride);
    if (res) {
      fprintf(stderr,
              "Error allocating destination buffer, see logs for details\n");
      return -1;
    }
    // Create a mock layer requesting a sinple copy of the pixels so that DoCopy gets called
    vsoc_hwc_layer src_layer;
    src_layer.compositionType = HWC_OVERLAY;
    src_layer.hints = 0;
    src_layer.flags = 0;
    src_layer.handle = src_handle;

    // No transformation, just a copy
    src_layer.transform = 0;
    src_layer.blending = HWC_BLENDING_NONE;

    src_layer.sourceCrop.top = 0;
    src_layer.sourceCrop.left = 0;
    src_layer.sourceCrop.bottom = 1;
    src_layer.sourceCrop.right = 1;

    src_layer.displayFrame.top = 0;
    src_layer.displayFrame.left = 0;
    src_layer.displayFrame.bottom = 1;
    src_layer.displayFrame.right = 1;

    src_layer.visibleRegionScreen.numRects = 0;
    src_layer.visibleRegionScreen.rects = NULL;

    src_layer.acquireFenceFd = -1;
    src_layer.releaseFenceFd = -1;
    // Call CompositeLayer
    CompositeLayer(&src_layer, dst_handle);
    // If we got this far without a SEGFAULT we call it success
    printf("OK\n");
    gralloc_dev_->device.free(&gralloc_dev_->device, src_handle);
    gralloc_dev_->device.free(&gralloc_dev_->device, dst_handle);
    return 0;
  }
};

int main() {
  HWC_Tester t;
  return t.RunTest();
}
