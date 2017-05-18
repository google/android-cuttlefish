/*
 * Copyright (C) 2016 The Android Open Source Project
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
#include "guest/ramdisk/compressed_file_reader.h"

#include <stdio.h>
#include <stdlib.h>
#include <cstdint>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

// We don't want an STL dependecy here, so just define this:
template <typename T> T min(T a, T b) {
  return (a < b) ? a : b;
}

CompressedFileReader::CompressedFileReader(const char* path) :
    fd_(open(path, O_RDONLY)),
    buffered_(0),
    used_(0),
    pos_(0),
    in_(NULL) {
  if (fd_.IsError()) {
    printf("%s: open(%s) failed %s:%d (%s)\n",
               __FUNCTION__, path, __FILE__, __LINE__,
               strerror(errno));
    return;
  }
  in_ = gzdopen(fd_, "rb");
  if (!in_) {
    printf("%s: gzdopen(%s) failed %s:%d (%s)\n", __FUNCTION__, path, __FILE__,
           __LINE__, strerror(errno));
    return;
  }
}

CompressedFileReader::~CompressedFileReader() {
  if (in_) {
    gzclose(in_);
  }
}

size_t CompressedFileReader::ReadBuffered(size_t len, char* dest) {
  if (!in_) {
    return 0;
  }
  if (used_ == buffered_) {
    NextBlock();
    if (used_ == buffered_) {
      return 0;
    }
  }
  size_t rval = min(len, buffered_ - used_);
  memcpy(dest, buffer_ + used_, rval);
  used_ += rval;
  pos_ += rval;
  return rval;
}

size_t CompressedFileReader::Read(size_t len, char* dest) {
  size_t rval = 0;
  while (len) {
    size_t num_read = ReadBuffered(len, dest);
    if (!num_read) {
      return rval;
    }
    len -= num_read;
    dest += num_read;
    rval += num_read;
  }
  return rval;
}

bool CompressedFileReader::Align(size_t alignment) {
  size_t to_skip = alignment - pos_ % alignment;
  if (to_skip == alignment) {
    return true;
  }
  return Skip(to_skip);
}

bool CompressedFileReader::Skip(size_t to_skip) {
  while (to_skip) {
    size_t skip = min(to_skip, buffered_ - used_);
    to_skip -= skip;
    used_ += skip;
    pos_ += skip;
    if (!to_skip) {
      break;
    }
    NextBlock();
    if (used_ == buffered_) {
      return false;
    }
  }
  return true;
}

uint64_t CompressedFileReader::Copy(
    uint64_t length, const char* path, int out_fd) {
  if (!in_) {
    return 0;
  }
  bool should_write = true;
  uint64_t total_read = 0;
  while (length) {
    if (used_ == buffered_) {
      NextBlock();
      if (used_ == buffered_) {
        return total_read;
      }
    }
    size_t num_read = min(length, static_cast<uint64_t>(buffered_ - used_));
    int num_written = 0;
    if (should_write) {
      num_written = TEMP_FAILURE_RETRY(
          write(out_fd, buffer_ + used_, num_read));
    }
    used_ += num_read;
    pos_ += num_read;
    total_read += num_read;
    length -= num_read;
    if (num_written == -1) {
      printf("%s: partial %s: write failed %s:%d (%s)\n",
             __FUNCTION__, path, __FILE__, __LINE__, strerror(errno));
      should_write = false;
    } else if (
        should_write && (static_cast<uint64_t>(num_written) != num_read)) {
      printf("%s: partial %s: write of %d, wanted %zu %s:%d (%s)\n",
             __FUNCTION__, path, num_written, num_read,
             __FILE__, __LINE__, strerror(errno));
      should_write = false;
    }
  }
  return total_read;
}

const char* CompressedFileReader::ErrorString() {
  const char* rval = gzerror(in_, NULL);
  if (!rval) {
    rval = "";
  }
  return rval;
}

void CompressedFileReader::NextBlock() {
  used_ = 0;
  buffered_ = gzread(in_, buffer_, sizeof(buffer_));
  if (buffered_ <= 0) {
    buffered_ = 0;
  }
}
