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
#include "common/metadata/display_properties.h"

#include <stdio.h>

namespace avd {
void DisplayProperties::Parse(const char* value) {
  if (!value) {
    return;
  }
  int xres, yres, unused_bits_per_pixel, dpi;
  int rval = sscanf(
      value, "%dx%dx%dx%d", &xres, &yres, &unused_bits_per_pixel, &dpi);
  // bits_per_pixel isn't really controllable, so do something sensible
  // if people stop setting it.
  if (rval == 3) {
    dpi = unused_bits_per_pixel;
  } else if (rval != 4) {
    return;
  }
  if ((xres < 0) || (yres < 0) || (dpi < 0)) {
    return;
  }
  x_res_ = xres;
  y_res_ = yres;
  // Bits per pixel is fixed at 32 in our devices.
  // bits_per_pixel_ = bits_per_pixel;
  dpi_ = dpi;
  default_ = false;
  config_.SetToString(value);
}
}  // namespace avd
