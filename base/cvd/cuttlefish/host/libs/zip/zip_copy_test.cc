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

#include "cuttlefish/host/libs/zip/zip_copy.h"

#include <string>
#include <vector>

#include "gmock/gmock-matchers.h"
#include "gtest/gtest.h"

#include "cuttlefish/host/libs/zip/libzip_cc/seekable_source.h"
#include "cuttlefish/host/libs/zip/zip_string.h"
#include "cuttlefish/result/result.h"
#include "cuttlefish/result/result_matchers.h"

namespace cuttlefish {
namespace {

TEST(ZipCopyTest, CopySmallBuffer) {
  std::string data_in = "test string";
  Result<WritableZipSource> in_source =
      WritableZipSource::BorrowData(data_in.data(), data_in.size());
  ASSERT_THAT(in_source, IsOk());

  std::vector<char> buffer(data_in.size());
  Result<WritableZipSource> out_source =
      WritableZipSource::BorrowData(buffer.data(), buffer.size());
  ASSERT_THAT(out_source, IsOk());

  EXPECT_THAT(Copy(*in_source, *out_source), IsOk());

  Result<std::string> data_out = ReadToString(*out_source);
  ASSERT_THAT(data_out, IsOk());

  EXPECT_EQ(data_in, *data_out);
}

}  // namespace
}  // namespace cuttlefish
