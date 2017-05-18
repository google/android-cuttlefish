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
#ifndef GUEST_RAMDISK_COMPRESSED_FILE_READER_H_
#define GUEST_RAMDISK_COMPRESSED_FILE_READER_H_

#include <AutoResources.h>
#include <zlib.h>

// Reads a compressed file.
class CompressedFileReader {
 public:

  // Open the given path.
  explicit CompressedFileReader(const char* path);

  virtual ~CompressedFileReader();

  // Read up to len bytes of buffered data. Will refill the buffer if it
  // is empty at the start.
  // Returns the number of bytes read or 0 at EOF.
  size_t ReadBuffered(size_t len, char* dest);

  // Read len bytes, only returning a short read at the EOF.
  // Returns the number of bytes read.
  size_t Read(size_t len, char* dest);

  // Skip bytes until the given alignment is readed.
  // Returns false if EOF was reached before the alignment was acheived.
  bool Align(size_t alignment);

  // Skip the given number of bytes.
  // Returns false if EOF was reached before the bytes were skipped.
  bool Skip(size_t to_skip);

  // Copy length bytes into the given file descriptor.
  // Will stop writing to the file descriptor on the first error.
  // Returns the number of bytes read. If this is < length then EOF was reached.
  uint64_t Copy(uint64_t length, const char* path, int out_fd);

  // Returns either the errror string from the decompressor or an empty string.
  const char* ErrorString();

 protected:
  // Read the next block into the internal buffer.
  void NextBlock();

  // Underlying file descriptor
  AutoCloseFileDescriptor fd_;

  // The internal buffer for the decompressor. libz doesn't work with small
  // sizes (~110 bytes), so we make this large and double buffer.
  char buffer_[8192];

  // Number of bytes currently in the buffer.
  size_t buffered_;
  // Number of bytes in the buffer that have already been returned.
  size_t used_;
  // File position.
  size_t pos_;
  // Decompressor context. Will be NULL if file object failed to initialize.
  gzFile in_;
};

#endif  // GUEST_RAMDISK_COMPRESSED_FILE_READER_H_
