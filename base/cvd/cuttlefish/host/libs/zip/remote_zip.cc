//
// Copyright (C) 2025 The Android Open Source Project
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

#include <errno.h>
#include <stdint.h>
#include <string.h>

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <fmt/core.h>

#include "cuttlefish/common/libs/utils/result.h"
#include "cuttlefish/host/libs/web/http_client/http_client.h"
#include "cuttlefish/host/libs/zip/zip_cc.h"

namespace cuttlefish {
namespace {

class RemoteZip : public SeekableZipSourceCallback {
 public:
  RemoteZip(HttpClient& http_client, std::string url, uint64_t size,
            std::vector<std::string> headers)
      : http_client_(http_client),
        url_(std::move(url)),
        size_(size),
        headers_(std::move(headers)) {}

  bool Close() override { return true; }
  bool Open() override {
    offset_ = 0;
    return true;
  }
  int64_t Read(char* zip_data, uint64_t zip_len) override {
    uint64_t already_read = 0;

    auto cb = [&already_read, &zip_data, &zip_len](char* http_data,
                                                   size_t http_len) -> bool {
      if (http_data == nullptr) {
        already_read = 0;
        return true;
      }
      if (http_len + already_read > zip_len) {
        return false;
      }
      memcpy(zip_data + already_read, http_data, http_len);
      already_read += http_len;
      return true;
    };
    std::vector<std::string> headers = headers_;
    headers.push_back(
        fmt::format("Range: bytes={}-{}", offset_, offset_ + zip_len));
    HttpRequest request = {
        .method = HttpMethod::kGet,
        .url = url_,
        .headers = headers,
    };
    Result<HttpResponse<void>> res =
        http_client_.DownloadToCallback(request, cb);
    if (!res.ok() || !res->HttpSuccess() || already_read != zip_len) {
      errno = EIO;
      return -1;
    }
    return already_read;
  }
  bool SetOffset(int64_t offset) override {
    offset_ = offset;
    return true;
  }
  int64_t Offset() override { return offset_; }
  uint64_t Size() override { return size_; }

 private:
  HttpClient& http_client_;
  std::string url_;
  uint64_t offset_ = 0;
  uint64_t size_ = 0;  // TODO: schuffelen - get this from a HEAD request
  std::vector<std::string> headers_;
};

}  // namespace

Result<ReadableZip> ZipFromUrl(HttpClient& http_client_, const std::string& url,
                               uint64_t size,
                               std::vector<std::string> headers) {
  std::unique_ptr<RemoteZip> callbacks =
      std::make_unique<RemoteZip>(http_client_, url, size, std::move(headers));
  CF_EXPECT(callbacks.get());

  SeekableZipSource source =
      CF_EXPECT(SeekableZipSource::FromCallbacks(std::move(callbacks)));

  return CF_EXPECT(ReadableZip::FromSource(std::move(source)));
}

}  // namespace cuttlefish
