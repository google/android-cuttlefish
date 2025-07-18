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

#include "cuttlefish/host/libs/zip/cached_zip_source.h"

#include <stddef.h>

#include <string>
#include <utility>

#include "gmock/gmock-matchers.h"
#include "gtest/gtest.h"

#include "cuttlefish/common/libs/utils/result.h"
#include "cuttlefish/common/libs/utils/result_matchers.h"
#include "cuttlefish/host/libs/zip/zip_cc.h"
#include "cuttlefish/host/libs/zip/zip_string.h"

namespace cuttlefish {
namespace {

TEST(CachedZipSourceTest, DataInCache) {
  std::string data_in = "test data";
  Result<WritableZipSource> inner =
      WritableZipSource::BorrowData(data_in.data(), data_in.size());
  ASSERT_THAT(inner, IsOk());

  Result<SeekableZipSource> cached =
      CacheZipSource(std::move(*inner), "temp_file");
  ASSERT_THAT(cached, IsOk());

  Result<std::string> data_out = ReadToString(*cached);
  EXPECT_THAT(data_out, IsOkAndValue(data_in));

  Result<std::string> data_out2 = ReadToString(*cached);
  EXPECT_THAT(data_out2, IsOkAndValue(data_in));
}

}  // namespace
}  // namespace cuttlefish
