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

// Define some of the string constants associated with the region layout.
#include "common/vsoc/shm/e2e_test_region_layout.h"

namespace vsoc {
namespace layout {
namespace e2e_test {

const char* E2EPrimaryTestRegionLayout::region_name = "e2e_primary";
const char
    E2EPrimaryTestRegionLayout::guest_pattern[E2EMemoryFill::kOwnedFieldSize] =
        "primary guest e2e string";
const char
    E2EPrimaryTestRegionLayout::host_pattern[E2EMemoryFill::kOwnedFieldSize] =
        "primary host e2e string";

const char* E2ESecondaryTestRegionLayout::region_name = "e2e_secondary";
const char E2ESecondaryTestRegionLayout::guest_pattern
    [E2EMemoryFill::kOwnedFieldSize] = "secondary guest e2e string";
const char
    E2ESecondaryTestRegionLayout::host_pattern[E2EMemoryFill::kOwnedFieldSize] =
        "secondary host e2e string";

const char* E2EUnfindableRegionLayout::region_name = "e2e_must_not_exist";

const char* E2EManagedTestRegionLayout::region_name = "e2e_managed";

const char* E2EManagerTestRegionLayout::region_name = "e2e_manager";

}  // namespace e2e_test
}  // namespace layout
}  // namespace vsoc
