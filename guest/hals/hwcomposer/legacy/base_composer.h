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

#include <functional>

#include <hardware/gralloc.h>
#include "hwcomposer_common.h"

namespace cvd {

using FbBroadcaster = std::function<void(int)>;

class BaseComposer {
 public:
  BaseComposer(int64_t vsync_base_timestamp, int32_t vsync_period_ns);
  ~BaseComposer();

  // Sets the composition type of each layer and returns the number of layers
  // to be composited by the hwcomposer.
  int PrepareLayers(size_t num_layers, vsoc_hwc_layer* layers);
  // Returns 0 if successful.
  int SetLayers(size_t num_layers, vsoc_hwc_layer* layers);
  // Changes the broadcaster, gives the ability to report more than just the
  // offset by using a wrapper like the StatsKeepingComposer. Returns the old
  // broadcaster. Passing a NULL pointer will cause the composer to not
  // broadcast at all.
  FbBroadcaster ReplaceFbBroadcaster(FbBroadcaster);
  void Dump(char* buff, int buff_len);

 protected:
  void Broadcast(int32_t offset);
  int NextScreenBuffer();

  const gralloc_module_t* gralloc_module_;
  int64_t vsync_base_timestamp_;
  int32_t vsync_period_ns_;
  int last_frame_buffer_ = -1; // The first index whill be 0

 private:
  // Returns buffer offset or negative on error.
  int PostFrameBufferTarget(buffer_handle_t handle);
  FbBroadcaster fb_broadcaster_;
};

}  // namespace cvd
