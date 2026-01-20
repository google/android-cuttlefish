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

#include "cuttlefish/pretty/numeric.h"

#include <stdint.h>

#include "cuttlefish/pretty/pretty.h"

namespace cuttlefish {

int8_t Pretty(int8_t value, PrettyAdlPlaceholder) { return value; }
int16_t Pretty(int16_t value, PrettyAdlPlaceholder) { return value; }
int32_t Pretty(int32_t value, PrettyAdlPlaceholder) { return value; }
int64_t Pretty(int64_t value, PrettyAdlPlaceholder) { return value; }

uint8_t Pretty(uint8_t value, PrettyAdlPlaceholder) { return value; }
uint16_t Pretty(uint16_t value, PrettyAdlPlaceholder) { return value; }
uint32_t Pretty(uint32_t value, PrettyAdlPlaceholder) { return value; }
uint64_t Pretty(uint64_t value, PrettyAdlPlaceholder) { return value; }

// By the standard, `char` is distinct from `signed char` and `unsigned char`.
char Pretty(char value, PrettyAdlPlaceholder) { return value; }

}  // namespace cuttlefish
