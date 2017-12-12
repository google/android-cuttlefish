#pragma once

/*
 * Copyright (C) 2017 The Android Open Source Project
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
#include "common/vsoc/shm/graphics.h"

namespace vsoc {

// Returns BytesPerPixel given a PixelFormat. Code should rely on this rather
// than accessing the mask directly.
static inline std::size_t BytesPerPixel(PixelFormat in) {
    return 1 + (in >> PixelFormatBase::SubformatSize) &
        (PixelFormatBase::MaxBytesPerPixel - 1);
}
}  // vsoc
