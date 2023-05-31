//
// Copyright 2021 The Android Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//

#pragma once

#include <cstdint>  // for uint8_t

namespace rootcanal {

enum class PacketType : uint8_t {
  UNKNOWN = 0,
  COMMAND = 1,
  ACL = 2,
  SCO = 3,
  EVENT = 4,
  ISO = 5,
};

}  // namespace rootcanal
