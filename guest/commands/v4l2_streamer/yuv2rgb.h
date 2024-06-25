/*
 * Copyright (C) 2024 The Android Open Source Project
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

namespace cuttlefish {

// Read from the given [src] buffer, expected to be in WebRTC YUV format,
// writing data the [dst] buffer in v4l2 BGRX32 format. [width] and [height]
// must be valid to describe the frame size, so that indexing calculations are
// accurate. Note that [src] and [dst] buffers are both required to be
// pre-allocated, [src] will need to contain valid YUV data, and [dst] contents
// will be overwritten.
void Yuv2Rgb(unsigned char *src, unsigned char *dst, int width, int height);

}  // namespace cuttlefish