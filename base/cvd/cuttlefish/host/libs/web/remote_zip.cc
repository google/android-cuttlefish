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

#include "cuttlefish/host/libs/web/remote_zip.h"

#include <errno.h>
#include <stdint.h>
#include <string.h>

#include <zip.h>

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <fmt/core.h>

#include "cuttlefish/common/libs/utils/result.h"
#include "cuttlefish/host/libs/web/http_client/http_client.h"

namespace cuttlefish {
namespace {

class ZipSource {
 public:
  ZipSource(HttpClient& http_client, std::string url, uint64_t size,
            std::vector<std::string> headers)
      : http_client_(http_client),
        url_(std::move(url)),
        size_(size),
        headers_(std::move(headers)) {
    zip_error_init(&last_error_);
  }

  ~ZipSource() { zip_error_fini(&last_error_); }

  int64_t Close() { return 0; }

  zip_error_t* Error() { return &last_error_; }

  int64_t Open() {
    offset_ = 0;
    return 0;
  }

  int64_t Read(char* const zip_data, const uint64_t zip_len) {
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
      zip_error_set(&last_error_, ZIP_ER_READ, EIO);
      return -1;
    }
    return already_read;
  }

  int64_t Seek(void* data, uint64_t data_length) {
    int64_t new_offset = zip_source_seek_compute_offset(
        offset_, size_, data, data_length, &last_error_);
    if (new_offset != -1) {
      offset_ = new_offset;
      return 0;
    } else {
      return -1;
    }
  }

  int64_t Stat(zip_stat_t* stat_out) {
    zip_stat_init(stat_out);  // TODO: schuffelen - deal with compression?
    stat_out->valid = ZIP_STAT_SIZE;
    stat_out->size = size_;
    return 0;
  }

  int64_t Tell() { return offset_; }

 private:
  HttpClient& http_client_;
  std::string url_;
  zip_error_t last_error_;
  uint64_t offset_ = 0;
  uint64_t size_ = 0;  // TODO: schuffelen - get this from a HEAD request
  std::vector<std::string> headers_;
};

// https://libzip.org/documentation/zip_source_function.html
int64_t ZipSourceCallback(void* userdata, void* data, uint64_t len,
                          zip_source_cmd_t cmd) {
  ZipSource* source = reinterpret_cast<ZipSource*>(userdata);
  zip_error_t* error = source->Error();
  switch (cmd) {
    case ZIP_SOURCE_CLOSE:
      return source->Close();
    case ZIP_SOURCE_ERROR:
      return zip_error_to_data(error, data, len);
    case ZIP_SOURCE_FREE:
      delete source;
      return 0;
    case ZIP_SOURCE_OPEN:
      return source->Open();
    case ZIP_SOURCE_READ:
      return source->Read(reinterpret_cast<char*>(data), len);
    case ZIP_SOURCE_SEEK: {
      return source->Seek(data, len);
    }
    case ZIP_SOURCE_STAT: {
      zip_stat_t* stat_data = ZIP_SOURCE_GET_ARGS(zip_stat_t, data, len, error);
      return stat_data ? source->Stat(stat_data) : -1;
    }
    case ZIP_SOURCE_SUPPORTS:
      return zip_source_make_command_bitmap(
          ZIP_SOURCE_CLOSE, ZIP_SOURCE_ERROR, ZIP_SOURCE_FREE, ZIP_SOURCE_OPEN,
          ZIP_SOURCE_READ, ZIP_SOURCE_SEEK, ZIP_SOURCE_STAT,
          ZIP_SOURCE_SUPPORTS, ZIP_SOURCE_TELL, -1);
    case ZIP_SOURCE_TELL:
      return source->Tell();
    default:
      zip_error_set(error, ZIP_ER_OPNOTSUPP, EINVAL);
      return -1;
  }
}

void FreeZipError(zip_error_t* error) {
  zip_error_fini(error);
  delete error;
}

}  // namespace

Result<std::unique_ptr<zip_t, void (*)(zip_t*)>> ZipFromUrl(
    HttpClient& http_client_, const std::string& url, uint64_t size,
    std::vector<std::string> headers) {
  std::unique_ptr<ZipSource> cpp_source =
      std::make_unique<ZipSource>(http_client_, url, size, std::move(headers));
  CF_EXPECT(cpp_source.get());

  std::unique_ptr<zip_error_t, void (*)(zip_error_t*)> error(new zip_error_t,
                                                             FreeZipError);
  zip_error_init(error.get());

  std::unique_ptr<zip_source_t, void (*)(zip_source_t*)> source(
      zip_source_function_create(ZipSourceCallback, cpp_source.get(),
                                 error.get()),
      zip_source_free);
  CF_EXPECT(source.get(), zip_error_strerror(error.get()));
  cpp_source.release();

  std::unique_ptr<zip_t, void (*)(zip_t*)> ret(
      zip_open_from_source(source.get(), ZIP_RDONLY, error.get()), zip_discard);
  CF_EXPECT(ret.get(), zip_error_strerror(error.get()));
  source.release();

  return ret;
}

}  // namespace cuttlefish
