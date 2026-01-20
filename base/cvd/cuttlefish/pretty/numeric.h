//
// Copyright (C) 2026 The Android Open Source Project
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

#include <stdint.h>

#include "cuttlefish/pretty/pretty.h"

namespace cuttlefish {

/**
 * The standard says that accessing unaligned values by reference is undefined
 * behavior. These overloads of `cuttlefish::Pretty` capture calls that use
 * numeric values and pass them by value rather than the default
 * pass-by-reference fallback defined in `pretty.h`. This is because numeric
 * values are likely to appear in `__attribute__((packed))` structs which can
 * have unaligned values.
 */

int8_t Pretty(int8_t, PrettyAdlPlaceholder unused = PrettyAdlPlaceholder());
int16_t Pretty(int16_t, PrettyAdlPlaceholder unused = PrettyAdlPlaceholder());
int32_t Pretty(int32_t, PrettyAdlPlaceholder unused = PrettyAdlPlaceholder());
int64_t Pretty(int64_t, PrettyAdlPlaceholder unused = PrettyAdlPlaceholder());

uint8_t Pretty(uint8_t, PrettyAdlPlaceholder unused = PrettyAdlPlaceholder());
uint16_t Pretty(uint16_t, PrettyAdlPlaceholder unused = PrettyAdlPlaceholder());
uint32_t Pretty(uint32_t, PrettyAdlPlaceholder unused = PrettyAdlPlaceholder());
uint64_t Pretty(uint64_t, PrettyAdlPlaceholder unused = PrettyAdlPlaceholder());

// By the standard, `char` is distinct from `signed char` and `unsigned char`.
char Pretty(char, PrettyAdlPlaceholder unused = PrettyAdlPlaceholder());

}  // namespace cuttlefish
