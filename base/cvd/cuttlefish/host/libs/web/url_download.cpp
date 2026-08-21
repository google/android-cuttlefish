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
#include <sys/stat.h>

#include <string>

#include "absl/log/log.h"
#include "fmt/format.h"

#include "cuttlefish/common/libs/fs/fd.h"
#include "cuttlefish/files/file_exists.h"
#include "cuttlefish/host/libs/web/http_client/http_client.h"
#include "cuttlefish/host/libs/web/http_client/http_file.h"
#include "cuttlefish/host/libs/web/http_client/scrub_secrets.h"
#include "cuttlefish/io/write_exact.h"
#include "cuttlefish/posix/remove.h"
#include "cuttlefish/posix/rename.h"
#include "cuttlefish/result/result.h"

namespace cuttlefish {
namespace {

constexpr int kLockAttempts = 4;

Result<void> FullDownload(HttpClient& http_client, const UrlDownload& download,
                          const std::string& path) {
  const HttpResponse<std::string> response = CF_EXPECT(
      HttpGetToFile(http_client, download.url, path, download.headers));
  CF_EXPECTF(response.HttpSuccess(), "'{}' - {}:{}", ScrubUrl(download.url),
             response.http_code, response.StatusDescription());
  return {};
}

// Resuming keeps the file HttpGetToFile would hide in a temporary: the offset
// an interrupted attempt left off at comes from that file, and the lock that
// serializes other `cvd` invocations sits on its descriptor.
Result<void> ResumeDownload(HttpClient& http_client,
                            const UrlDownload& download, Fd& part,
                            const std::string& part_path,
                            const std::string& path) {
  const uint64_t have =
      CF_EXPECTF(part.SeekEnd(0), "Could not measure '{}'", part_path);

  uint64_t offset = 0;
  if (download.size.has_value() && have < *download.size) {
    offset = have;
  }
  // Anything past the offset belongs to a download of something else.
  CF_EXPECTF(part.Truncate(offset), "Could not truncate '{}'", part_path);
  CF_EXPECTF(part.SeekSet(offset), "Could not seek '{}'", part_path);

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

}  // namespace

Result<bool> HoldsFileAt(Fd& fd, const std::string& path) {
  // The descriptor is open before the lock says whose file it is, so comparing
  // two paths would race with the rename that ends another download.
  struct stat by_path = {};
  if (stat(path.c_str(), &by_path) != 0) {
    return false;
  }
  const struct stat by_fd = CF_EXPECTF(fd.Fstat(), "Could not read '{}'", path);
  return by_path.st_dev == by_fd.st_dev && by_path.st_ino == by_fd.st_ino;
}

Result<void> DownloadUrlToFile(HttpClient& http_client,
                               const UrlDownload& download,
                               const std::string& path) {
  // Without something to resume against, a unique temporary file per attempt
  // keeps concurrent downloads of the same artifact out of each other's way.
  if (!download.resumable) {
    CF_EXPECT(FullDownload(http_client, download, path));
    return {};
  }

  const std::string part_path = fmt::format("{}.part", path);
  // The lock serializes other `cvd` invocations downloading this artifact into
  // the shared generation-keyed cache; a fetch itself is single-threaded.
  for (int attempt = 0; attempt < kLockAttempts; attempt++) {
    Fd part = CF_EXPECT(Fd::Open(part_path, O_RDWR | O_CREAT, 0644));
    CF_EXPECTF(part.Flock(LOCK_EX), "Could not lock '{}'", part_path);

    if (CF_EXPECT(HoldsFileAt(part, part_path))) {
      CF_EXPECT(ResumeDownload(http_client, download, part, part_path, path));
      return {};
    }
    // Another download of this artifact renamed the partial file away while
    // this one waited for its lock.
    if (FileExists(path)) {
      return {};
    }
  }
  return CF_ERRF("Gave up waiting for another download of '{}'", part_path);
}

}  // namespace cuttlefish
