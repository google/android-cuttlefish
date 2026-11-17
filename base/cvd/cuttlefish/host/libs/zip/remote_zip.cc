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

#include <algorithm>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "absl/log/log.h"
#include "fmt/core.h"
#include "fmt/format.h"

#include "cuttlefish/host/libs/web/http_client/http_client.h"
#include "cuttlefish/host/libs/web/http_client/http_probe.h"
#include "cuttlefish/host/libs/zip/libzip_cc/seekable_source.h"
#include "cuttlefish/io/fake_seek.h"
#include "cuttlefish/io/io.h"
#include "cuttlefish/io/visitable.h"
#include "cuttlefish/result/result.h"

namespace cuttlefish {
namespace {

class RemoteZip : public ReaderFakeSeeker {
 public:
  RemoteZip(HttpClient& http_client, std::string url, uint64_t size,
            std::vector<std::string> headers)
      : ReaderFakeSeeker(size),
        http_client_(http_client),
        url_(std::move(url)),
        size_(size),
        headers_(std::move(headers)) {}

  Result<void> Visit(IoVisitor& visitor) override {
    CF_EXPECT(visitor.Accept(*this));
    return {};
  }

  Result<uint64_t, int> PRead(void* data, uint64_t len,
                              uint64_t offset) const override {
    if (len == 0 || offset >= size_) {
      return 0;
    }
    len = std::min(len, size_ - offset);

    uint64_t already_read = 0;

    auto cb = [&already_read, &data, &len](char* http_data,
                                           size_t http_len) -> Result<void> {
      if (http_data == nullptr) {
        already_read = 0;
        return {};
      }
      CF_EXPECT_LE(http_len + already_read, len);
      memcpy(reinterpret_cast<char*>(data) + already_read, http_data, http_len);
      already_read += http_len;
      return {};
    };
    std::vector<std::string> headers = headers_;
    headers.push_back(
        fmt::format("Range: bytes={}-{}", offset, offset + len - 1));
    VLOG(1) << "Requesting " << headers.back();
    HttpRequest request = {
        .method = HttpMethod::kGet,
        .url = url_,
        .headers = headers,
    };
    HttpResponse<void> res =
        CF_EXPECTV(http_client_.DownloadToCallback(request, cb), EIO,
                   "HTTP library issue");
    CF_EXPECTVF(res.HttpSuccess(), EIO, "unexpected code: {}", res.http_code);
    CF_EXPECTVF(already_read == len, EIO, "{} != {}", already_read, len);
    return already_read;
  }

 private:
  HttpClient& http_client_;
  std::string url_;
  uint64_t size_ = 0;
  std::vector<std::string> headers_;
};

Result<uint64_t> GetSizeIfSupportsRangeRequests(
    HttpClient& http_client_, const std::string& url,
    const std::vector<std::string>& headers) {
  const HttpObjectInfo info =
      CF_EXPECT(ProbeHttpObject(http_client_, url, headers));
  CF_EXPECT(!!info.accept_ranges);
  CF_EXPECT(info.size.has_value());
  return *info.size;
}

}  // namespace

Result<SeekableZipSource> ZipSourceFromUrl(HttpClient& http_client,
                                           const std::string& url,
                                           std::vector<std::string> headers,
                                           std::optional<uint64_t> size) {
  if (!size.has_value()) {
    size = CF_EXPECT(GetSizeIfSupportsRangeRequests(http_client, url, headers));
  }
  std::unique_ptr<RemoteZip> callbacks =
      std::make_unique<RemoteZip>(http_client, url, *size, std::move(headers));
  CF_EXPECT(callbacks.get());

  return CF_EXPECT(SeekableZipSource::FromCallbacks(std::move(callbacks)));
}

}  // namespace cuttlefish
