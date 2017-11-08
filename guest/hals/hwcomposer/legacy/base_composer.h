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

#ifndef GCE_OLD_HWCOMPOSER_GCE_COMPOSER_H
#define GCE_OLD_HWCOMPOSER_GCE_COMPOSER_H

#include <hardware/gralloc.h>
#include "hwcomposer_common.h"

namespace avd {

typedef int (*FbBroadcaster)(int);

class BaseComposer {
 public:
  BaseComposer(int64_t vsync_base_timestamp, int32_t vsync_period_ns);
  ~BaseComposer();

  // Sets the composition type of each layer and returns the number of layers
  // to be composited by the hwcomposer.
  int PrepareLayers(size_t num_layers, gce_hwc_layer* layers);
  // Returns the yoffset that was broadcasted or a negative number if there was
  // an error.
  int SetLayers(size_t num_layers, gce_hwc_layer* layers);
  // Returns yoffset of the handle or negative on error.
  int PostFrameBuffer(buffer_handle_t handle);
  // Changes the broadcaster, gives the ability to report more than just the
  // yoffset by using a wrapper like the StatsKeepingComposer. Returns the old
  // broadcaster. Passing a NULL pointer will cause the composer to not
  // broadcast at all.
  FbBroadcaster ReplaceFbBroadcaster(FbBroadcaster);
  void Dump(char* buff, int buff_len);
 protected:
  int64_t vsync_base_timestamp_;
  int32_t vsync_period_ns_;
 private:
  FbBroadcaster fb_broadcaster_;
};

}  // namespace avd

#endif
