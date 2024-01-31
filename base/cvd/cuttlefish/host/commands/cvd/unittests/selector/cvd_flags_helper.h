/*
 * Copyright (C) 2023 The Android Open Source Project
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

#include <optional>
#include <string>
#include <variant>

#include <gtest/gtest.h>

#include "common/libs/utils/result.h"
#include "host/commands/cvd/flag.h"
#include "host/commands/cvd/types.h"

namespace cuttlefish {

// use this only when std::optional is not nullopt
template <typename T>
static Result<T> Get(
    const std::optional<FlagCollection::ValueVariant>& opt_var) {
  CF_EXPECT(opt_var != std::nullopt);
  std::variant<std::int32_t, bool, std::string> var = *opt_var;
  auto* ptr = std::get_if<T>(&var);
  CF_EXPECT(ptr != nullptr);
  return *ptr;
}

class CvdFlagCollectionTest : public testing::Test {
 protected:
  CvdFlagCollectionTest();

  cvd_common::Args input_;
  FlagCollection flag_collection_;
};

}  // namespace cuttlefish
