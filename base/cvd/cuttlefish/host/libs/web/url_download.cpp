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

#include <errno.h>
#include <fcntl.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <string>

#include "absl/log/log.h"
#include "absl/strings/str_cat.h"
#include "android-base/unique_fd.h"
#include "fmt/format.h"

#include "cuttlefish/common/libs/fs/shared_buf.h"
#include "cuttlefish/common/libs/fs/shared_fd.h"
#include "cuttlefish/common/libs/utils/files.h"
#include "cuttlefish/files/file_exists.h"
#include "cuttlefish/host/libs/web/http_client/http_client.h"
#include "cuttlefish/host/libs/web/http_client/http_file.h"
#include "cuttlefish/host/libs/web/http_client/scrub_secrets.h"
#include "cuttlefish/posix/remove.h"
#include "cuttlefish/result/result.h"

namespace cuttlefish {
namespace {

constexpr int kLockAttempts = 4;

Result<void> FullDownload(HttpClient& http_client, const UrlDownload& download,
                          const std::string& path) {
  HttpResponse<std::string> response = CF_EXPECT(
      HttpGetToFile(http_client, download.url, path, download.headers));
  CF_EXPECTF(response.HttpSuccess(), "'{}' - {}:{}", ScrubUrl(download.url),
             response.http_code, response.StatusDescription());
  return {};
}

Result<void> ResumeDownload(HttpClient& http_client,
                            const UrlDownload& download, const SharedFD& part,
                            const std::string& part_path,
                            const std::string& path) {
  off_t have = part->LSeek(0, SEEK_END);
  CF_EXPECTF(have >= 0, "Could not measure '{}' - {}", part_path,
             part->StrError());

  uint64_t offset = 0;
  if (download.size.has_value() && have > 0 &&
      static_cast<uint64_t>(have) < *download.size) {
    offset = have;
  }
  // Anything past the offset belongs to a download of something else.
  CF_EXPECTF(part->Truncate(offset), "Could not truncate '{}'", part_path);
  CF_EXPECTF(part->LSeek(offset, SEEK_SET) >= 0, "Could not seek '{}' - {}",
             part_path, part->StrError());

  while (true) {
    HttpRequest request = {
        .method = HttpMethod::kGet,
        .url = download.url,
        .headers = download.headers,
    };
    if (offset > 0) {
      request.headers.push_back(fmt::format("Range: bytes={}-", offset));
      if (!download.if_range.empty()) {
        request.headers.push_back(
            absl::StrCat("If-Range: ", download.if_range));
      }
    }

    uint64_t written = 0;
    auto callback = [&part, &part_path, offset, &written](char* data,
                                                          size_t size) -> bool {
      // A retry inside the client starts the response over from the range that
      // was asked for.
      if (data == nullptr) {
        written = 0;
        if (Result<void> truncated = part->Truncate(offset);
            !truncated.has_value()) {
          LOG(ERROR) << truncated.error();
          return false;
        }
        return part->LSeek(offset, SEEK_SET) >= 0;
      }
      if (WriteAll(part, data, size) != static_cast<ssize_t>(size)) {
        LOG(ERROR) << "Could not write '" << part_path
                   << "': " << part->StrError();
        return false;
      }
      written += size;
      return true;
    };

    HttpResponse<void> response =
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

  CF_EXPECT(RenameFile(part_path, path));
  return {};
}

}  // namespace

Result<bool> HoldsFileAt(SharedFD fd, const std::string& path) {
  struct stat by_path = {};
  if (stat(path.c_str(), &by_path) != 0) {
    return false;
  }
  android::base::unique_fd raw(fd->UNMANAGED_Dup());
  CF_EXPECTF(raw.get() >= 0, "Could not duplicate the descriptor of '{}'",
             path);
  struct stat by_fd = {};
  CF_EXPECTF(fstat(raw.get(), &by_fd) == 0, "Could not read '{}' - {}", path,
             strerror(errno));
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

  const std::string part_path = absl::StrCat(path, ".part");
  for (int attempt = 0; attempt < kLockAttempts; attempt++) {
    SharedFD part = SharedFD::Open(part_path, O_RDWR | O_CREAT, 0644);
    CF_EXPECTF(part->IsOpen(), "Could not open '{}' - {}", part_path,
               part->StrError());
    CF_EXPECTF(part->Flock(LOCK_EX), "Could not lock '{}'", part_path);

    const bool holds_part = CF_EXPECT(HoldsFileAt(part, part_path));
    if (holds_part) {
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
