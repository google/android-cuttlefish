//
// Copyright (C) 2019 The Android Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#pragma once

#include <string>
#include <vector>

#include "cuttlefish/result/result.h"

namespace cuttlefish {

/**
 * Converts any Android-Sparse image files in `image_files` to raw image files.
 *
 * Android-Sparse is a file format invented by Android that optimizes for
 * chunks of zeroes or repeated data. The Android build system can produce
 * sparse files to save on size of disk files after they are extracted from a
 * disk file, as the imag eflashing process also can handle Android-Sparse
 * images.
 *
 * crosvm has read-only support for Android-Sparse files, but QEMU does not
 * support them.
 */
Result<void> DeAndroidSparse2(const std::vector<std::string>& image_files);

}  // namespace cuttlefish
