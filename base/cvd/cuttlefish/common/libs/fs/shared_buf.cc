/*
 * Copyright (C) 2019 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "cuttlefish/common/libs/fs/shared_buf.h"

#include <string>
#include <string_view>
#include <vector>

#include "cuttlefish/common/libs/fs/shared_fd.h"

namespace cuttlefish {

ssize_t WriteAll(SharedFD fd, const char* buf, size_t size) {
  size_t total_written = 0;
  Result<uint64_t> write_res;
  do {
    write_res = fd->Write((void*)&(buf[total_written]), size - total_written);
    if (!write_res.has_value()) {
      errno = fd->GetErrno();
      return -1;
    }
    if (*write_res == 0) {
      return total_written;
    }
    total_written += *write_res;
  } while (total_written < size);
  return total_written;
}

ssize_t ReadExact(SharedFD fd, char* buf, size_t size) {
  size_t total_read = 0;
  Result<uint64_t> read_res;
  do {
    read_res = fd->Read((void*)&(buf[total_read]), size - total_read);
    if (!read_res.has_value()) {
      errno = fd->GetErrno();
      return -1;
    }
    if (*read_res == 0) {
      return total_read;
    }
    total_read += *read_res;
  } while (total_read < size);
  return total_read;
}

ssize_t ReadExact(SharedFD fd, std::string* buf) {
  return ReadExact(fd, buf->data(), buf->size());
}

ssize_t ReadExact(SharedFD fd, std::vector<char>* buf) {
  return ReadExact(fd, buf->data(), buf->size());
}

ssize_t WriteAll(SharedFD fd, std::string_view buf) {
  return WriteAll(fd, buf.data(), buf.size());
}

ssize_t WriteAll(SharedFD fd, const std::vector<char>& buf) {
  return WriteAll(fd, buf.data(), buf.size());
}

bool SendAll(SharedFD sock, std::string_view msg) {
  ssize_t total_written{};
  if (!sock->IsOpen()) {
    return false;
  }
  while (total_written < static_cast<ssize_t>(msg.size())) {
    auto just_written = sock->Send(msg.data() + total_written,
                                   msg.size() - total_written, MSG_NOSIGNAL);
    if (just_written <= 0) {
      return false;
    }
    total_written += just_written;
  }
  return true;
}

}  // namespace cuttlefish
