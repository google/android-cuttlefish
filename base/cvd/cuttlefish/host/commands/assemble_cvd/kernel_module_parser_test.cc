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

#include "cuttlefish/host/commands/assemble_cvd/kernel_module_parser.h"

#include "gmock/gmock-matchers.h"
#include "gtest/gtest.h"

#include "cuttlefish/io/owning_in_memory_io.h"
#include "cuttlefish/result/result_matchers.h"

namespace cuttlefish {
namespace {

TEST(KernelModuleParserTest, IsSigned) {
  OwningInMemoryIo file("something\n~Module signature appended~\n");

  EXPECT_THAT(IsKernelModuleSigned(file), IsOkAndValue(true));
}

TEST(KernelModuleParserTest, IsNotSigned) {
  OwningInMemoryIo file("something\n~Module signature not appended~\n");

  EXPECT_THAT(IsKernelModuleSigned(file), IsOkAndValue(false));
}

TEST(KernelModuleParserTest, EmptyIsNotSigned) {
  OwningInMemoryIo file;

  EXPECT_THAT(IsKernelModuleSigned(file), IsOkAndValue(false));
}

}  // namespace
}  // namespace cuttlefish
