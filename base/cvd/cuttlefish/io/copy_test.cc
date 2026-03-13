//
// Copyright (C) 2023 The Android Open Source Project
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

#include "cuttlefish/io/copy.h"

#include <memory>
#include <string>
#include <vector>

#include "gmock/gmock-matchers.h"
#include "gtest/gtest.h"

#include "cuttlefish/io/in_memory.h"
#include "cuttlefish/io/read_exact.h"
#include "cuttlefish/result/result_matchers.h"

namespace cuttlefish {
namespace {

TEST(CopyTest, CopySmallBuffer) {
  std::vector<char> data = {1, 2, 3, 4, 5};

  std::unique_ptr<ReaderWriterSeeker> in = InMemoryIo(data);
  std::unique_ptr<ReaderWriterSeeker> out = InMemoryIo();

  EXPECT_THAT(Copy(*in, *out), IsOk());

  std::vector<char> data_out(data.size());

  EXPECT_THAT(PReadExact(*out, data_out.data(), data_out.size(), 0), IsOk());
  EXPECT_EQ(data, data_out);
}

}  // namespace
}  // namespace cuttlefish
