// Copyright (C) 2024 The Android Open Source Project
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

#include "image.h"

#include <vector>

namespace cuttlefish {

std::vector<uint8_t> CreateImageContentsWithFourCorners(
    uint32_t width, uint32_t height, const RGBA8888& bottomLeft,
    const RGBA8888& bottomRight, const RGBA8888& topLeft,
    const RGBA8888& topRight) {
  std::vector<uint8_t> ret;
  ret.reserve(width * height * 4);

  const RGBA8888* grid[2][2] = {
      {&topLeft, &bottomLeft},
      {&topRight, &bottomRight},
  };

  for (uint32_t y = 0; y < height; y++) {
    const bool isBotHalf = (y <= (height / 2));
    for (uint32_t x = 0; x < width; x++) {
      const bool isLeftHalf = (x <= (width / 2));

      const RGBA8888* color = grid[isLeftHalf ? 0 : 1][isBotHalf ? 0 : 1];
      ret.push_back(color->r);
      ret.push_back(color->g);
      ret.push_back(color->b);
      ret.push_back(color->a);
    }
  }
  return ret;
}

}  // namespace cuttlefish