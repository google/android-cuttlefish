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

#pragma once

#include <vector>

namespace cuttlefish {

struct RGBA8888 {
  uint8_t r;
  uint8_t g;
  uint8_t b;
  uint8_t a;
};

// NOTE: Adjusts for the Vulkan coordinate system with (-1, -1) at the top left:
//
//   const std::vector<uint8_t> contents = CreateImageContentsWithFourCorners(
//          /*width=*/2,
//          /*height=*/2,
//          /*bottomLeft=*/<RED>,
//          /*bottomRight=*/<BLUE>,
//          /*topLeft=*/<GREEN>,
//          /*topRight=*/<BLACK>);
//
//   contents[ 0 through  3] == <GREEN>
//   contents[ 4 through  7] == <BLACK>
//   contents[ 8 through 11] == <RED>
//   contents[12 through 15] == <BLUE>
std::vector<uint8_t> CreateImageContentsWithFourCorners(
    uint32_t width, uint32_t height, const RGBA8888& bottomLeft,
    const RGBA8888& bottomRight, const RGBA8888& topLeft,
    const RGBA8888& topRight);

}  // namespace cuttlefish