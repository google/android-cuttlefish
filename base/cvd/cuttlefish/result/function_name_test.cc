/*
 * Copyright (C) 2021 The Android Open Source Project
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

#include "cuttlefish/result/function_name.h"

#include <functional>
#include <source_location>
#include <utility>

#include "gtest/gtest.h"

#include "cuttlefish/result/result_type.h"

std::source_location GlobalFunction() {
  return std::source_location::current();
}

namespace cuttlefish {
namespace {

class Class {
 public:
  std::source_location ConstFn() const volatile {
    return std::source_location::current();
  }
  std::source_location operator<(int) const {
    return std::source_location::current();
  }
  std::source_location operator+(int) const {
    return std::source_location::current();
  }
};

std::source_location SimpleSignature() {
  return std::source_location::current();
}

Result<std::pair<std::source_location, std::function<int(int)>>>
ComplexSignature(std::function<int(int)> arg) {
  return std::make_pair(std::source_location::current(), arg);
}

TEST(FunctionName, ExtractSignature) {
  EXPECT_EQ(FunctionName(SimpleSignature()), "SimpleSignature");

  EXPECT_EQ(FunctionName(ComplexSignature([](int i) { return i; })->first),
            "ComplexSignature");

  EXPECT_EQ(FunctionName(GlobalFunction()), "GlobalFunction");

  Class instance;
  EXPECT_EQ(FunctionName(instance.ConstFn()), "ConstFn");
  EXPECT_EQ(FunctionName(instance < 5), "operator");
  EXPECT_EQ(FunctionName(instance + 5), "operator+");

  auto anonymous_fn = []() { return std::source_location::current(); };
  EXPECT_EQ(FunctionName(anonymous_fn()), "operator");
}

}  // namespace
}  // namespace cuttlefish
