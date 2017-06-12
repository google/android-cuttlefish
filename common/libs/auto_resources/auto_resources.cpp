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
#include "common/libs/auto_resources/auto_resources.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>

bool AutoCloseFILE::CopyFrom(const AutoCloseFILE& in) {
  char buffer[8192];
  while (!in.IsEOF()) {
    size_t num_read = fread(buffer, 1, sizeof(buffer), in);
    if (!num_read) {
      if (in.IsEOF()) {
        return true;
      }
      printf("%s: unable to fread %s:%d (%s)\n",
             __FUNCTION__, __FILE__, __LINE__, strerror(errno));
      return false;
    }
    size_t num_written = fwrite(buffer, 1, num_read, *this);
    if (num_written != num_read) {
      printf("%s: unable to fwrite, %zu != %zu %s:%d (%s)\n",
             __FUNCTION__, num_read, num_written, __FILE__, __LINE__,
             strerror(errno));
      return false;
    }
  }
  return true;
}

AutoFreeBuffer::~AutoFreeBuffer() {
  if (data_) free(data_);
}

void AutoFreeBuffer::Clear() {
  size_ = 0;
}

bool AutoFreeBuffer::Reserve(size_t newsize) {
  if (newsize > reserve_size_ ||
      reserve_size_ > kAutoBufferShrinkReserveThreshold) {
    char* newdata = static_cast<char*>(realloc(data_, newsize));
    // If realloc fails, everything remains unchanged.
    if (!newdata && newsize) return false;

    reserve_size_ = newsize;
    data_ = newdata;
  }
  if (size_ > newsize) size_ = newsize;
  return true;
}

bool AutoFreeBuffer::Resize(size_t newsize) {
  // If reservation is small, and we get a shrink request, simply reduce size_.
  if (reserve_size_ < kAutoBufferShrinkReserveThreshold && newsize < size_) {
    size_ = newsize;
    return true;
  }

  if (!Reserve(newsize)) return false;

  // Should we keep this? Sounds like it should be called Grow().
  if (newsize > size_) memset(&data_[size_], 0, newsize - size_);
  size_ = newsize;
  return true;
}

bool AutoFreeBuffer::SetToString(const char* in) {
  size_t newsz = strlen(in) + 1;
  if (!Resize(newsz)) return false;
  memcpy(data_, in, newsz);
  return true;
}

bool AutoFreeBuffer::Append(const void* new_data, size_t new_data_size) {
  size_t offset = size_;
  if (!Resize(offset + new_data_size)) return false;
  memcpy(&data_[offset], new_data, new_data_size);
  return true;
}

size_t AutoFreeBuffer::PrintF(const char* format, ... ) {
  va_list args;

  // Optimize: Use whatever reservation left we have for initial printf.
  // If reservation is not long enough, resize and try again.

  va_start(args, format);
  size_t printf_size = vsnprintf(data_, reserve_size_, format, args);
  va_end(args);

  // vsnprintf write no more than |reserve_size_| bytes including trailing \0.
  // Result value equal or greater than |reserve_size_| signals truncated
  // output.
  if (printf_size < reserve_size_) {
    size_ = printf_size + 1;
    return printf_size;
  }

  // Grow buffer and re-try printf.
  if (!Resize(printf_size + 1)) return 0;
  va_start(args, format);
  vsprintf(data_, format, args);
  va_end(args);
  return printf_size;
}

