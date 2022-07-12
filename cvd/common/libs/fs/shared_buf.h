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
#pragma once

#include <string>
#include <thread>
#include <vector>

#include "common/libs/fs/shared_fd.h"

namespace cuttlefish {

/**
 * Reads from fd until it is closed or errors, storing all data in buf.
 *
 * On a successful read, returns the number of bytes read.
 *
 * If a read error is encountered, returns -1. buf will contain any data read
 * up until that point and errno will be set.
 *
 */
ssize_t ReadAll(SharedFD fd, std::string* buf);

/**
 * Reads from fd until reading buf->size() bytes or errors.
 *
 * On a successful read, returns buf->size().
 *
 * If a read error is encountered, returns -1. buf will contain any data read
 * up until that point and errno will be set.
 *
 * If the size of buf is 0, read(fd, buf, 0) is effectively called, which means
 * error(s) might be detected. If detected, the return value would be -1.
 * If not detected, the return value will be 0.
 *
 */
ssize_t ReadExact(SharedFD fd, std::string* buf);

/**
 * Reads from fd until reading buf->size() bytes or errors.
 *
 * On a successful read, returns buf->size().
 *
 * If a read error is encountered, returns -1. buf will contain any data read
 * up until that point and errno will be set.
 *
 * If the size of buf is 0, read(fd, buf, 0) is effectively called, which means
 * error(s) might be detected. If detected, the return value would be -1.
 * If not detected, the return value will be 0.
 *
 */
ssize_t ReadExact(SharedFD fd, std::vector<char>* buf);

/**
 * Reads from fd until reading `size` bytes or errors.
 *
 * On a successful read, returns buf->size().
 *
 * If a read error is encountered, returns -1. buf will contain any data read
 * up until that point and errno will be set.
 *
 * When the size is 0, read(fd, buf, 0) is effectively called, which means
 * error(s) might be detected. If detected, the return value would be -1.
 * If not detected, the return value will be 0.
 *
 */
ssize_t ReadExact(SharedFD fd, char* buf, size_t size);

/*
 * Reads from fd until reading `sizeof(T)` bytes or errors.
 *
 * On a successful read, returns `sizeof(T)`.
 *
 * If a read error is encountered, returns -1. buf will contain any data read
 * up until that point and errno will be set.
 */
template<typename T>
ssize_t ReadExactBinary(SharedFD fd, T* binary_data) {
  return ReadExact(fd, (char*) binary_data, sizeof(*binary_data));
}

/**
 * Writes to fd until writing all bytes in buf.
 *
 * On a successful write, returns buf.size().
 *
 * If a write error is encountered, returns -1. Some data may have already been
 * written to fd at that point.
 *
 * If the size of buf is 0, WriteAll returns 0 with no error set unless
 * the fd is a regular file. If fd is a regular file, write(fd, buf, 0) is
 * effectively called. It may detect errors; if detected, errno is set and
 * -1 is returned. If not detected, 0 is returned with errno unchanged.
 *
 */
ssize_t WriteAll(SharedFD fd, const std::string& buf);

/**
 * Writes to fd until writing all bytes in buf.
 *
 * On a successful write, returns buf.size().
 *
 * If a write error is encountered, returns -1. Some data may have already been
 * written to fd at that point.
 *
 * If the size of buf is 0, WriteAll returns 0 with no error set unless
 * the fd is a regular file. If fd is a regular file, write(fd, buf, 0) is
 * effectively called. It may detect errors; if detected, errno is set and
 * -1 is returned. If not detected, 0 is returned with errno unchanged.
 *
 */
ssize_t WriteAll(SharedFD fd, const std::vector<char>& buf);

/**
 * Writes to fd until `size` bytes are written from `buf`.
 *
 * On a successful write, returns `size`.
 *
 * If a write error is encountered, returns -1. Some data may have already been
 * written to fd at that point.
 *
 * If size is 0, WriteAll returns 0 with no error set unless
 * the fd is a regular file. If fd is a regular file, write(fd, buf, 0) is
 * effectively called. It may detect errors; if detected, errno is set and
 * -1 is returned. If not detected, 0 is returned with errno unchanged.
 *
 */
ssize_t WriteAll(SharedFD fd, const char* buf, size_t size);

/**
 * Writes to fd until `sizeof(T)` bytes are written from binary_data.
 *
 * On a successful write, returns `sizeof(T)`.
 *
 * If a write error is encountered, returns -1. Some data may have already been
 * written to fd at that point.
 *
 * If ever sizeof(T) is 0, WriteAll returns 0 with no error set unless
 * the fd is a regular file. If fd is a regular file, write(fd, buf, 0) is
 * effectively called. It may detect errors; if detected, errno is set and
 * -1 is returned. If not detected, 0 is returned with errno unchanged.
 *
 */
template<typename T>
ssize_t WriteAllBinary(SharedFD fd, const T* binary_data) {
  return WriteAll(fd, (const char*) binary_data, sizeof(*binary_data));
}

/**
 * Sends contents of msg through sock, checking for socket error conditions
 *
 * On successful Send, returns true
 *
 * If a Send error is encountered, returns false. Some data may have already
 * been written to 'sock' at that point.
 */
bool SendAll(SharedFD sock, const std::string& msg);

/**
 * Receives 'count' bytes from sock, checking for socket error conditions
 *
 * On successful Recv, returns a string containing the received data
 *
 * If a Recv error is encountered, returns the empty string
 */
std::string RecvAll(SharedFD sock, const size_t count);

} // namespace cuttlefish
