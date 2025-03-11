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

#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "common/libs/fs/shared_buf.h"
#include "common/libs/fs/shared_fd.h"

namespace cuttlefish {

namespace {

const size_t BUFF_SIZE = 1 << 14;

} // namespace

ssize_t WriteAll(SharedFD fd, const char* buf, size_t size) {
  size_t total_written = 0;
  ssize_t written = 0;
  do {
    written = fd->Write((void*)&(buf[total_written]), size - total_written);
    if (written <= 0) {
      if (written < 0) {
        errno = fd->GetErrno();
        return written;
      }
      return total_written;
    }
    total_written += written;
  } while (total_written < size);
  return total_written;
}

ssize_t ReadExact(SharedFD fd, char* buf, size_t size) {
  size_t total_read = 0;
  ssize_t read = 0;
  do {
    read = fd->Read((void*)&(buf[total_read]), size - total_read);
    if (read <= 0) {
      if (read < 0) {
        errno = fd->GetErrno();
        return read;
      }
      return total_read;
    }
    total_read += read;
  } while (total_read < size);
  return total_read;
}

ssize_t ReadAll(SharedFD fd, std::string* buf) {
  char buff[BUFF_SIZE];
  std::stringstream ss;
  ssize_t read;
  while ((read = fd->Read(buff, BUFF_SIZE - 1)) > 0) {
    // this is necessary to avoid problems with having a '\0' in the middle of the buffer
    ss << std::string(buff, read);
  }
  if (read < 0) {
    errno = fd->GetErrno();
    return read;
  }
  *buf = ss.str();
  return buf->size();
}

ssize_t ReadExact(SharedFD fd, std::string* buf) {
  return ReadExact(fd, buf->data(), buf->size());
}

ssize_t ReadExact(SharedFD fd, std::vector<char>* buf) {
  return ReadExact(fd, buf->data(), buf->size());
}

ssize_t WriteAll(SharedFD fd, const std::string& buf) {
  return WriteAll(fd, buf.data(), buf.size());
}

ssize_t WriteAll(SharedFD fd, const std::vector<char>& buf) {
  return WriteAll(fd, buf.data(), buf.size());
}

bool SendAll(SharedFD sock, const std::string& msg) {
  ssize_t total_written{};
  if (!sock->IsOpen()) {
    return false;
  }
  while (total_written < static_cast<ssize_t>(msg.size())) {
    auto just_written = sock->Send(msg.c_str() + total_written,
                                   msg.size() - total_written, MSG_NOSIGNAL);
    if (just_written <= 0) {
      return false;
    }
    total_written += just_written;
  }
  return true;
}

std::string RecvAll(SharedFD sock, const size_t count) {
  size_t total_read{};
  if (!sock->IsOpen()) {
    return {};
  }
  std::unique_ptr<char[]> data(new char[count]);
  while (total_read < count) {
    auto just_read = sock->Read(data.get() + total_read, count - total_read);
    if (just_read <= 0) {
      return {};
    }
    total_read += just_read;
  }
  return {data.get(), count};
}

} // namespace cuttlefish
