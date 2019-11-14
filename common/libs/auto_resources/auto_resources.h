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
#ifndef CUTTLEFISH_COMMON_COMMON_LIBS_AUTO_RESOURCES_AUTO_RESOURCES_H_
#define CUTTLEFISH_COMMON_COMMON_LIBS_AUTO_RESOURCES_AUTO_RESOURCES_H_

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/stat.h>

template <typename T, size_t N>
char (&ArraySizeHelper(T (&array)[N]))[N];

template <typename T, size_t N>
char (&ArraySizeHelper(const T (&array)[N]))[N];

#define arraysize(array) (sizeof(ArraySizeHelper(array)))

// Automatically close a file descriptor
class AutoCloseFILE {
 public:
  explicit AutoCloseFILE(FILE *f) : f_(f) { }
  virtual ~AutoCloseFILE() {
    if (f_) {
      (void)::fclose(f_);
      f_ = NULL;
    }
  }

  operator FILE*() const {
    return f_;
  }

  bool CopyFrom(const AutoCloseFILE& in);

  bool IsError() const {
    return f_ == NULL;
  }

  bool IsEOF() const {
    return IsError() || feof(f_);
  }

  bool IsOpen() const {
    return f_ != NULL;
  }

  // Close the underlying file descriptor, returning a status to give the caller
  // the chance to act on failure to close.
  // Returns true on success.
  bool close() {
    bool rval = true;
    if (f_) {
      rval = !::fclose(f_);
      f_ = NULL;
    }
    return rval;
  }

 private:
  AutoCloseFILE& operator=(const AutoCloseFILE & o);
  explicit AutoCloseFILE(const AutoCloseFILE &);

  FILE* f_;
};

// Automatically close a file descriptor
class AutoCloseFileDescriptor {
 public:
  explicit AutoCloseFileDescriptor(int fd) : fd_(fd) { }
  virtual ~AutoCloseFileDescriptor() {
    if (fd_ != -1) {
      (void)::close(fd_);
      fd_ = -1;
    }
  }

  operator int() const {
    return fd_;
  }

  bool IsError() const {
    return fd_ == -1;
  }

  // Close the underlying file descriptor, returning a status to give the caller
  // the chance to act on failure to close.
  // Returns true on success.
  bool close() {
    bool rval = true;
    if (fd_ != -1) {
      rval = !::close(fd_);
      fd_ = -1;
    }
    return rval;
  }

 private:
  AutoCloseFileDescriptor& operator=(const AutoCloseFileDescriptor & o);
  explicit AutoCloseFileDescriptor(const AutoCloseFileDescriptor &);

  int fd_;
};

// In C++11 this is just std::vector<char>, but Android isn't
// there yet.
class AutoFreeBuffer {
 public:
  enum {
    // Minimum reserve size of AutoFreeBuffer to consider shrinking reservation.
    // Any buffer shorter than this will not be shrunk.
    kAutoBufferShrinkReserveThreshold = 8192
  };

  AutoFreeBuffer()
      : data_(NULL), size_(0), reserve_size_(0) {}

  AutoFreeBuffer(size_t reserve_size)
      : data_(NULL), size_(0), reserve_size_(0) {
    Reserve(reserve_size);
  }

  ~AutoFreeBuffer();
  void Clear();
  bool Resize(size_t newsize);
  bool Reserve(size_t newsize);
  bool SetToString(const char* in);
  bool Append(const void* new_data, size_t new_data_size);
  size_t PrintF(const char* format, ... );

  char* data() {
    return data_;
  }

  const char* data() const {
    return data_;
  }

  char* begin() {
    return data_;
  }

  const char* begin() const {
    return data_;
  }

  char* end() {
    return data_ + size_;
  }

  const char* end() const {
    return data_ + size_;
  }

  size_t size() const {
    return size_;
  }

  size_t reserve_size() const {
    return reserve_size_;
  }

  void Swap(AutoFreeBuffer& other) {
    char* temp_ptr = data_;
    data_ = other.data_;
    other.data_ = temp_ptr;

    size_t temp_size = size_;
    size_ = other.size_;
    other.size_ = temp_size;

    temp_size = reserve_size_;
    reserve_size_ = other.reserve_size_;
    other.reserve_size_ = temp_size;
  }

  bool operator==(const AutoFreeBuffer& other) const {
    return (size_ == other.size_) && !memcmp(data_, other.data_, size_);
  }

  bool operator!=(const AutoFreeBuffer& other) const {
    return !(*this == other);
  }

 protected:
  char *data_;
  size_t size_;
  size_t reserve_size_;

 private:
  AutoFreeBuffer& operator=(const AutoFreeBuffer&);
  explicit AutoFreeBuffer(const AutoFreeBuffer&);
};
#endif  // CUTTLEFISH_COMMON_COMMON_LIBS_AUTO_RESOURCES_AUTO_RESOURCES_H_
