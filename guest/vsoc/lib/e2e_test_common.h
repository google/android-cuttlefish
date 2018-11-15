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

#pragma once

#include <android-base/logging.h>

#define DEATH_TEST_MESSAGE "abort converted to exit of 2 during death test"

static inline void disable_tombstones() {
  // We don't want a tombstone, and we're already in the child, so we modify the
  // behavior of LOG(ABORT) to print the well known message and do an
  // error-based exit.
  android::base::SetAborter([](const char*) {
    fputs(DEATH_TEST_MESSAGE, stderr);
    fflush(stderr);
    exit(2);
  });
}
