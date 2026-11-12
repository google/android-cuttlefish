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

#include "cuttlefish/host/libs/zip/remote_zip.h"

#include <stdlib.h>

#include <map>
#include <memory>
#include <string>
#include <utility>

#include "gmock/gmock-matchers.h"
#include "gtest/gtest.h"

#include "cuttlefish/host/libs/web/http_client/fake_http_client.h"
#include "cuttlefish/host/libs/zip/libzip_cc/archive.h"
#include "cuttlefish/host/libs/zip/libzip_cc/seekable_source.h"
#include "cuttlefish/host/libs/zip/zip_over_ranges.h"
#include "cuttlefish/io/reader_seeker.h"
#include "cuttlefish/io/string.h"
#include "cuttlefish/result/assert.h"
#include "cuttlefish/result/result_matchers.h"

namespace cuttlefish {
namespace {

TEST(RemoteZipTest, TwoFiles) {
  FakeHttpClient http_client;

  std::map<std::string, std::string> zip_contents = {
      std::make_pair("a.txt", "abc"), std::make_pair("b.txt", "def")};

  http_client.SetResponse(CF_ASSERT(ZipOverRanges::Create(zip_contents)));

  SeekableZipSource src = CF_ASSERT(ZipSourceFromUrl(http_client, "url", {}));
  ReadableZip remote_zip = CF_ASSERT(ReadableZip::FromSource(std::move(src)));

  std::unique_ptr<ReaderSeeker> file_a =
      CF_ASSERT(remote_zip.OpenReadOnly("a.txt"));
  ASSERT_TRUE(file_a.get());

  ASSERT_THAT(ReadToString(*file_a), IsOkAndValue("abc"));

  std::unique_ptr<ReaderSeeker> file_b =
      CF_ASSERT(remote_zip.OpenReadOnly("b.txt"));
  ASSERT_TRUE(file_b);
  ASSERT_THAT(ReadToString(*file_b), IsOkAndValue("def"));
}

}  // namespace
}  // namespace cuttlefish
