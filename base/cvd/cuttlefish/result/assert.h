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

#include "gmock/gmock.h"  // IWYU pragma: keep: preprocessor

#include "cuttlefish/result/expect.h"  // IWYU pragma: keep: preprocessor
#include "cuttlefish/result/result_matchers.h"  // IWYU pragma: keep: preprocessor

#define CF_ASSERT_OVERLOAD(_1, _2, NAME, ...) NAME

#define CF_ASSERT2(RESULT, MSG)                               \
  ({                                                          \
    decltype(RESULT)&& macro_intermediate_result = RESULT;    \
    ASSERT_THAT(macro_intermediate_result, IsOk()) << MSG;    \
    OutcomeDereference(std::move(macro_intermediate_result)); \
  })

#define CF_ASSERT1(RESULT) CF_ASSERT2(RESULT, "")

#define CF_ASSERT(...) \
  CF_ASSERT_OVERLOAD(__VA_ARGS__, CF_ASSERT2, CF_ASSERT1)(__VA_ARGS__)
