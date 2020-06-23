/*
 * Copyright (C) 2019 The Android Open Source Project
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

#include "guest/hals/hwcomposer/common/screen_view.h"

#include "common/libs/utils/size_utils.h"

namespace cuttlefish {

int ScreenView::NextBuffer() {
  int num_buffers = this->num_buffers();
  last_buffer_ = num_buffers > 0 ? (last_buffer_ + 1) % num_buffers : -1;
  return last_buffer_;
}

size_t ScreenView::buffer_size() const {
  return line_length() * y_res() + 4 /* swiftshader padding */;
}

size_t ScreenView::line_length() const {
  return cuttlefish::AlignToPowerOf2(x_res() * bytes_per_pixel(), 4);
}

int ScreenView::bytes_per_pixel() const { return 4; }
}  // namespace cuttlefish
