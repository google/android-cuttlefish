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
#include <stddef.h>
#include <stdint.h>
#include <sys/file.h>

#include <string>

#include "absl/log/log.h"
#include "fmt/format.h"

#include "cuttlefish/common/libs/fs/fd.h"
#include "cuttlefish/host/libs/web/http_client/http_client.h"
#include "cuttlefish/host/libs/web/http_client/http_file.h"
#include "cuttlefish/host/libs/web/http_client/scrub_secrets.h"
#include "cuttlefish/io/write_exact.h"
#include "cuttlefish/posix/remove.h"
#include "cuttlefish/posix/rename.h"
#include "cuttlefish/result/result.h"

namespace cuttlefish {
namespace {

Result<void> FullDownload(HttpClient& http_client, const UrlDownload& download,
                          const std::string& path) {
  const HttpResponse<std::string> response = CF_EXPECT(
      HttpGetToFile(http_client, download.url, path, download.headers));
  CF_EXPECTF(response.HttpSuccess(), "'{}' - {}:{}", ScrubUrl(download.url),
             response.http_code, response.StatusDescription());
  return {};
}

}  // namespace

Result<void> DownloadUrlToFile(HttpClient& http_client,
                               const UrlDownload& download,
                               const std::string& path) {
  // Without something to resume against, a unique temporary file per attempt
  // keeps concurrent downloads of the same artifact out of each other's way.
  if (!download.resumable) {
    CF_EXPECT(FullDownload(http_client, download, path));
    return {};
  }

  // Resuming keeps the file HttpGetToFile would hide in a temporary: the
  // offset an interrupted attempt left off at comes from that file, and the
  // lock that serializes other `cvd` invocations sits on its descriptor.
  const std::string part_path = fmt::format("{}.part", path);
  Fd part = CF_EXPECT(Fd::Open(part_path, O_RDWR | O_CREAT, 0644));
  CF_EXPECTF(part.Flock(LOCK_EX), "Could not lock '{}'", part_path);

  const uint64_t have =
      CF_EXPECTF(part.SeekEnd(0), "Could not measure '{}'", part_path);

  uint64_t offset = 0;
  if (download.size.has_value() && have < *download.size) {
    offset = have;
  }

  uint64_t written = 0;
  uint64_t last_log = 0;
  while (true) {
    HttpRequest request = {
        .method = HttpMethod::kGet,
        .url = download.url,
        .headers = download.headers,
    };
    if (offset > 0) {
      request.headers.push_back(fmt::format("Range: bytes={}-", offset));
      if (download.if_range.has_value()) {
        request.headers.push_back(
            fmt::format("If-Range: {}", *download.if_range));
      }
    }

    auto callback = [&part, &part_path, offset, &written, &last_log](
                        char* data, size_t size) -> bool {
      // A retry inside the client starts the response over from the range that
      // was asked for.
      if (data == nullptr) {
        written = 0;
        last_log = 0;
        if (Result<void> truncated = part.Truncate(offset);
            !truncated.has_value()) {
          LOG(ERROR) << truncated.error();
          return false;
        }
        return part.SeekSet(offset).has_value();
      }
      if (Result<void> written_data = WriteExact(part, data, size);
          !written_data.has_value()) {
        LOG(ERROR) << "Could not write '" << part_path
                   << "': " << written_data.error();
        return false;
      }
      written += size;
      if (written / 2 >= last_log) {
        VLOG(0) << "Downloaded " << offset + written << " bytes";
        last_log = written;
      }
      return true;
    };

    const HttpResponse<void> response =
        CF_EXPECT(http_client.DownloadToCallback(request, callback));
    if (!response.HttpSuccess()) {
      // The body of an error response is not the artifact.
      CF_EXPECT(RemoveFile(part_path));
      return CF_ERRF("'{}' - {}:{}", ScrubUrl(download.url), response.http_code,
                     response.StatusDescription());
    }
    if (offset == 0 || !download.size.has_value() ||
        offset + written == *download.size) {
      break;
    }
    LOG(WARNING) << "'" << ScrubUrl(download.url)
                 << "' answered a resumed request with the whole object";
    offset = 0;
  }

  VLOG(0) << "Downloaded '" << offset + written << "' total bytes from '"
          << ScrubUrl(download.url) << "' to '" << path << "'.";
  CF_EXPECT(Rename(part_path, path));
  return {};
}

}  // namespace cuttlefish
