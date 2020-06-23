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

#include "guest/hals/hwcomposer/common/geometry_utils.h"

#include <algorithm>
#include <utility>

namespace cuttlefish {

bool LayersOverlap(const hwc_layer_1_t& layer1, const hwc_layer_1_t& layer2) {
  int left1 = layer1.displayFrame.left;
  int right1 = layer1.displayFrame.right;
  int top1 = layer1.displayFrame.top;
  int bottom1 = layer1.displayFrame.bottom;

  int left2 = layer2.displayFrame.left;
  int right2 = layer2.displayFrame.right;
  int top2 = layer2.displayFrame.top;
  int bottom2 = layer2.displayFrame.bottom;

  bool overlap_x = left1 < right2 && left2 < right1;
  bool overlap_y = top1 < bottom2 && top2 < bottom1;

  return overlap_x && overlap_y;
}

}  // namespace cuttlefish
