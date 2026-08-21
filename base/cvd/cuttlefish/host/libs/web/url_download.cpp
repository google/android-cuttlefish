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

#include "cuttlefish/host/libs/web/url_download.h"

#include <fcntl.h>
#include <stdint.h>
#include <sys/file.h>

#include <algorithm>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/log/log.h"
#include "fmt/format.h"

#include "cuttlefish/common/libs/fs/fd.h"
#include "cuttlefish/host/libs/web/digest.h"
#include "cuttlefish/host/libs/web/http_client/http_client.h"
#include "cuttlefish/host/libs/web/http_client/http_file.h"
#include "cuttlefish/host/libs/web/http_client/scrub_secrets.h"
#include "cuttlefish/host/libs/zip/libzip_cc/seekable_source.h"
#include "cuttlefish/host/libs/zip/remote_zip.h"
#include "cuttlefish/io/write_exact.h"
#include "cuttlefish/posix/remove.h"
#include "cuttlefish/posix/rename.h"
#include "cuttlefish/result/result.h"

namespace cuttlefish {
namespace {

constexpr uint64_t kReadSize = 64 << 20;

Result<void> FullDownload(HttpClient& http_client, const UrlDownload& download,
                          const std::string& path) {
  const HttpResponse<std::string> response = CF_EXPECT(
      HttpGetToFile(http_client, download.url, path, download.headers));
  CF_EXPECTF(response.HttpSuccess(), "'{}' - {}:{}", ScrubUrl(download.url),
             response.http_code, response.StatusDescription());
  return {};
}

}  // namespace

std::string PartialFilePath(const std::string& path, std::string_view version) {
  return fmt::format("{}.{}.part", path, Sha256Hex(version).substr(0, 16));
}

Result<void> DownloadUrlToFile(HttpClient& http_client,
                               const UrlDownload& download,
                               const std::string& path) {
  // Without something to resume against, a unique temporary file per attempt
  // keeps concurrent downloads of the same artifact out of each other's way.
  if (!download.version.has_value() || !download.size.has_value()) {
    CF_EXPECT(FullDownload(http_client, download, path));
    return {};
  }

  // Resuming keeps the file HttpGetToFile would hide in a temporary: the
  // offset an interrupted attempt left off at comes from that file, and the
  // lock that serializes other `cvd` invocations sits on its descriptor.
  const std::string part_path = PartialFilePath(path, *download.version);
  Fd part = CF_EXPECT(Fd::Open(part_path, O_RDWR | O_CREAT, 0644));
  CF_EXPECTF(part.Flock(LOCK_EX), "Could not lock '{}'", part_path);

  const uint64_t size = *download.size;
  uint64_t offset =
      CF_EXPECTF(part.SeekEnd(0), "Could not measure '{}'", part_path);
  // A partial file is shorter than the object; anything else starts over.
  if (offset >= size) {
    offset = 0;
    CF_EXPECTF(part.Truncate(0), "Could not truncate '{}'", part_path);
    CF_EXPECTF(part.SeekSet(0), "Could not seek '{}'", part_path);
  }

  std::vector<std::string> headers = download.headers;
  if (download.if_range.has_value()) {
    headers.push_back(fmt::format("If-Range: {}", *download.if_range));
  }
  SeekableZipSource source = CF_EXPECT(
      ZipSourceFromUrl(http_client, download.url, std::move(headers), size));
  SeekingZipSourceReader reader = CF_EXPECT(source.Reader());
  CF_EXPECT(reader.SeekSet(offset));

  std::vector<char> buffer(std::min(kReadSize, size - offset));
  while (offset < size) {
    const uint64_t length = std::min<uint64_t>(buffer.size(), size - offset);
    Result<uint64_t> chunk = reader.Read(buffer.data(), length);
    if (!chunk.has_value() && offset == 0) {
      CF_EXPECT(RemoveFile(part_path));
    }
    const uint64_t read_length = CF_EXPECTF(
        std::move(chunk), "Could not download '{}'", ScrubUrl(download.url));
    CF_EXPECTF(read_length > 0, "'{}' ended after {} of {} bytes",
               ScrubUrl(download.url), offset, size);
    CF_EXPECTF(WriteExact(part, buffer.data(), read_length),
               "Could not write '{}'", part_path);
    offset += read_length;
    VLOG(0) << "Downloaded " << offset << " of " << size << " bytes";
  }

  VLOG(0) << "Downloaded '" << ScrubUrl(download.url) << "' to '" << path
          << "'.";
  CF_EXPECT(Rename(part_path, path));
  return {};
}

}  // namespace cuttlefish
