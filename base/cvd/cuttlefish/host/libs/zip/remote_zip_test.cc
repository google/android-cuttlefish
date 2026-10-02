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
#include "cuttlefish/io/io.h"
#include "cuttlefish/io/string.h"
#include "cuttlefish/result/result.h"
#include "cuttlefish/result/result_matchers.h"

namespace cuttlefish {
namespace {

TEST(RemoteZipTest, TwoFiles) {
  FakeHttpClient http_client;

  std::map<std::string, std::string> zip_contents = {
      std::make_pair("a.txt", "abc"), std::make_pair("b.txt", "def")};

  Result<ZipOverRanges> zip_handler = ZipOverRanges::Create(zip_contents);
  ASSERT_THAT(zip_handler, IsOk());

  http_client.SetResponse(std::move(*zip_handler));

  Result<SeekableZipSource> source = ZipSourceFromUrl(http_client, "url", {});
  ASSERT_THAT(source, IsOk());
  Result<ReadableZip> remote_zip = ReadableZip::FromSource(std::move(*source));
  ASSERT_THAT(remote_zip, IsOk());

  Result<std::unique_ptr<ReaderSeeker>> file_a =
      remote_zip->OpenReadOnly("a.txt");
  ASSERT_THAT(file_a, IsOk());
  ASSERT_NE(file_a->get(), nullptr);
  ASSERT_THAT(ReadToString(**file_a), IsOkAndValue("abc"));

  Result<std::unique_ptr<ReaderSeeker>> file_b =
      remote_zip->OpenReadOnly("b.txt");
  ASSERT_THAT(file_b, IsOk());
  ASSERT_NE(file_b->get(), nullptr);
  ASSERT_THAT(ReadToString(**file_b), IsOkAndValue("def"));
}

}  // namespace
}  // namespace cuttlefish
