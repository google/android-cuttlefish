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

// Adapters for using SharedFD as std::istream and std::ostream.

#ifndef CUTTLEFISH_COMMON_COMMON_LIBS_FS_SHARED_FD_STREAM_H_
#define CUTTLEFISH_COMMON_COMMON_LIBS_FS_SHARED_FD_STREAM_H_

#include <cstdio>
#include <istream>
#include <memory>
#include <ostream>
#include <streambuf>

#include "common/libs/fs/shared_fd.h"

namespace cuttlefish {

class SharedFDStreambuf : public std::streambuf {
 public:
  SharedFDStreambuf(SharedFD shared_fd);

 private:
  // Reading characters from the SharedFD.
  int underflow() override;
  std::streamsize xsgetn(char* dest, std::streamsize count) override;

  // Write characters to the SharedFD.
  int overflow(int c) override;
  std::streamsize xsputn(const char* source, std::streamsize count) override;

  int pbackfail(int c) override;

 private:
  SharedFD shared_fd_;

  static constexpr const ptrdiff_t kUngetSize = 128;
  static constexpr const ptrdiff_t kBufferSize = 4096 + kUngetSize;
  std::unique_ptr<char[]> read_buffer_ = nullptr;
};

class SharedFDIstream : public std::istream {
 public:
  SharedFDIstream(SharedFD shared_fd);

 private:
  SharedFDStreambuf buf_;
};

class SharedFDOstream : public std::ostream {
 public:
  SharedFDOstream(SharedFD shared_fd);

 private:
  SharedFDStreambuf buf_;
};

}  // namespace cuttlefish

#endif