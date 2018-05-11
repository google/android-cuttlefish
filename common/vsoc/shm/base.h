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

#include <stdint.h>

// Base macros for all layout structures.

namespace vsoc {
namespace layout {

/**
 * Memory is shared between Guest and Host kernels. In some cases we need
 * flag to indicate which side we're on. In those cases we'll use this
 * simple structure.
 *
 * These are carefully formatted to make Guest and Host a bitfield.
 */
enum Sides : uint32_t {
  NoSides = 0,
  Guest = 1,
  Host = 2,
  Both = 3,
#ifdef CUTTLEFISH_HOST
  OurSide = Host,
  Peer = Guest
#else
  OurSide = Guest,
  Peer = Host
#endif
};

/**
 * Base class for all region layout structures.
 */
class RegionLayout {
};

}  // namespace layout
}  // namespace vsoc
