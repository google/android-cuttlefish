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

#include <stdint.h>
#include <stdlib.h>

#include <zip.h>

#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/strings/match.h"
#include "absl/strings/numbers.h"
#include "absl/strings/str_split.h"
#include "gmock/gmock-matchers.h"
#include "gtest/gtest.h"

#include "cuttlefish/common/libs/utils/result.h"
#include "cuttlefish/common/libs/utils/result_matchers.h"
#include "cuttlefish/host/libs/web/http_client/fake_http_client.h"
#include "cuttlefish/host/libs/web/http_client/http_client.h"

namespace cuttlefish {
namespace {

void FreeZipError(zip_error_t* error) {
  zip_error_fini(error);
  delete error;
}

class InMemoryZip {
 public:
  static Result<InMemoryZip> Create(
      const std::map<std::string, std::string>& contents) {
    std::string data(4096, '\0');

    std::unique_ptr<zip_error_t, void (*)(zip_error_t*)> error(new zip_error_t,
                                                               FreeZipError);
    zip_error_init(error.get());

    std::unique_ptr<zip_source_t, void (*)(zip_source_t*)> source(
        zip_source_buffer_create(data.data(), data.size(), 0, error.get()),
        zip_source_free);
    CF_EXPECT(source.get(), zip_error_strerror(error.get()));
    zip_source_keep(source.get());

    std::unique_ptr<zip_t, void (*)(zip_t*)> zip_ptr(
        zip_open_from_source(source.get(), ZIP_CREATE | ZIP_TRUNCATE,
                             error.get()),
        zip_discard);
    if (zip_ptr.get() == nullptr) {
      zip_source_free(source.get());  // balances zip_source_keep
    }
    CF_EXPECT(zip_ptr.get(), zip_error_strerror(error.get()));

    for (const auto& [path, data] : contents) {
      std::unique_ptr<zip_source_t, void (*)(zip_source_t*)> source(
          zip_source_buffer(zip_ptr.get(), data.data(), data.size(), 0),
          zip_source_free);
      CF_EXPECT(source.get());

      CF_EXPECT_NE(zip_file_add(zip_ptr.get(), path.c_str(), source.get(), 0),
                   -1, zip_error_strerror(zip_get_error(zip_ptr.get())));
      source.release();
    }

    CF_EXPECT_EQ(zip_close(zip_ptr.get()), 0,
                 zip_error_strerror(zip_get_error(zip_ptr.get())));
    zip_ptr.release();

    CF_EXPECT_EQ(zip_source_open(source.get()), 0,
                 zip_error_strerror(zip_source_error(source.get())));
    int64_t bytes_read =
        zip_source_read(source.get(), data.data(), data.size());
    CF_EXPECT_NE(bytes_read, -1,
                 zip_error_strerror(zip_source_error(source.get())));
    data.resize(bytes_read, 0);
    CF_EXPECT_EQ(zip_source_close(source.get()), 0,
                 zip_error_strerror(zip_source_error(source.get())));

    return InMemoryZip(std::move(data));
  }

  std::string operator()(const HttpRequest& request) {
    static constexpr std::string_view kPrefix = "Range: bytes=";
    std::string range;
    for (const std::string& header : request.headers) {
      if (absl::StartsWith(header, kPrefix)) {
        range = header.substr(kPrefix.size());
      }
    }
    size_t start = 0;
    size_t end = data_.size();
    if (!range.empty()) {
      std::vector<std::string_view> range_parts = absl::StrSplit(range, "-");
      if (range_parts.size() == 2) {
        if (!absl::SimpleAtoi(range_parts[0], &start) ||
            !absl::SimpleAtoi(range_parts[1], &end)) {
          start = 0;
          end = data_.size();
        }
      }
    }
    if (end > data_.size()) {
      end = data_.size();
    }
    return data_.substr(start, end - start);
  }

  size_t Size() { return data_.size(); }

 private:
  InMemoryZip(std::string data) : data_(std::move(data)) {}

  std::string data_;
};

TEST(RemoteZipTest, TwoFiles) {
  FakeHttpClient http_client;

  std::map<std::string, std::string> zip_contents = {
      std::make_pair("a.txt", "abc"), std::make_pair("b.txt", "def")};

  Result<InMemoryZip> zip_in = InMemoryZip::Create(zip_contents);
  ASSERT_THAT(zip_in, IsOk());
  size_t size = zip_in->Size();

  http_client.SetResponse(std::move(*zip_in));

  Result<std::unique_ptr<zip_t, void (*)(zip_t*)>> remote_zip =
      ZipFromUrl(http_client, "url", size, {});
  ASSERT_THAT(remote_zip, IsOk());
  ASSERT_NE(remote_zip->get(), nullptr);

  std::unique_ptr<zip_file_t, int (*)(zip_file_t*)> file_a(
      zip_fopen(remote_zip->get(), "a.txt", 0), zip_fclose);
  ASSERT_NE(file_a.get(), nullptr)
      << zip_error_strerror(zip_get_error(remote_zip->get()));

  std::string a_contents(1024, '\0');
  a_contents.resize(
      zip_fread(file_a.get(), a_contents.data(), a_contents.size()));
  EXPECT_EQ(a_contents, "abc");

  std::unique_ptr<zip_file_t, int (*)(zip_file_t*)> file_b(
      zip_fopen(remote_zip->get(), "b.txt", 0), zip_fclose);
  ASSERT_NE(file_b.get(), nullptr)
      << zip_error_strerror(zip_get_error(remote_zip->get()));

  std::string b_contents(1024, '\0');
  b_contents.resize(
      zip_fread(file_b.get(), b_contents.data(), b_contents.size()));
  EXPECT_EQ(b_contents, "def");
}

}  // namespace
}  // namespace cuttlefish
