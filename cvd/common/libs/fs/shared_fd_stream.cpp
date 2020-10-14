/*
 * Copyright (C) 2020 The Android Open Source Project
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

#include "common/libs/fs/shared_fd_stream.h"

#include <cstdio>
#include <streambuf>

#include "common/libs/fs/shared_buf.h"

namespace cuttlefish {

SharedFDStreambuf::SharedFDStreambuf(SharedFD shared_fd)
  : shared_fd_(shared_fd) {}

int SharedFDStreambuf::underflow() {
  if (gptr() < egptr()) {
    return *gptr();
  }

  size_t unget_size = 0;
  constexpr size_t bytes_to_read = kBufferSize - kUngetSize;
  if (read_buffer_ == nullptr) {
    read_buffer_ = std::make_unique<char[]>(kBufferSize);
  } else {
    unget_size = std::min(gptr() - eback(), kUngetSize);
    std::memcpy(read_buffer_.get(),
                read_buffer_.get() + kBufferSize - unget_size,
                unget_size);
  }

  ssize_t bytes_read = ReadExact(shared_fd_,
                                 read_buffer_.get() + unget_size,
                                 bytes_to_read);

  setg(read_buffer_.get(),
       read_buffer_.get() + unget_size,
       read_buffer_.get() + unget_size + bytes_read);

  if (bytes_read <= 0 || in_avail() == 0) {
    return EOF;
  }

  return static_cast<int>(*gptr());
}

std::streamsize SharedFDStreambuf::xsgetn(char* dst, std::streamsize count) {
  std::streamsize bytes_read = 0;
  while (bytes_read < count) {
    if (in_avail() == 0) {
      if (underflow() == EOF) {
        break;
      }
    }
    std::streamsize buffer_count =
        std::min(static_cast<std::streamsize>(in_avail()), count - bytes_read);
    std::memcpy(dst + bytes_read, gptr(), buffer_count);
    gbump(buffer_count);
    bytes_read += buffer_count;
  }
  return bytes_read;
}

int SharedFDStreambuf::overflow(int c) {
  if (c != EOF) {
      char z = c;
      if (WriteAll(shared_fd_, &z, 1) != 1) {
          return EOF;
      }
  }
  return c;
}

std::streamsize SharedFDStreambuf::xsputn(const char* src,
                                          std::streamsize count) {
  return static_cast<std::streamsize>(
    WriteAll(shared_fd_, src, static_cast<std::size_t>(count)));
}

int SharedFDStreambuf::pbackfail(int c) {
  if (c != EOF) {
    if (gptr() != eback()) {
      gbump(-1);
      *(gptr()) = c;
      return c;
    }
  }
  return EOF;
}

SharedFDOstream::SharedFDOstream(SharedFD shared_fd)
  : std::ostream(nullptr), buf_(shared_fd) { rdbuf(&buf_); }

SharedFDIstream::SharedFDIstream(SharedFD shared_fd)
  : std::istream(nullptr), buf_(shared_fd) { rdbuf(&buf_); }

} // namespace cuttlefish