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

#include <string>
#include <thread>
#include <vector>

#include "common/libs/fs/shared_fd.h"

namespace cvd {

/**
 * Reads from fd until it is closed or errors, storing all data in buf.
 *
 * On a successful read, returns the number of bytes read.
 *
 * If a read error is encountered, returns -1. buf will contain any data read
 * up until that point and errno will be set.
 */
ssize_t ReadAll(SharedFD fd, std::string* buf);

/**
 * Reads from fd until reading buf->size() bytes or errors.
 *
 * On a successful read, returns buf->size().
 *
 * If a read error is encountered, returns -1. buf will contain any data read
 * up until that point and errno will be set.
 */
ssize_t ReadExact(SharedFD fd, std::string* buf);

/**
 * Reads from fd until reading buf->size() bytes or errors.
 *
 * On a successful read, returns buf->size().
 *
 * If a read error is encountered, returns -1. buf will contain any data read
 * up until that point and errno will be set.
 */
ssize_t ReadExact(SharedFD fd, std::vector<char>* buf);

/**
 * Writes to fd until writing all bytes in buf.
 *
 * On a successful write, returns buf.size().
 *
 * If a write error is encountered, returns -1. Some data may have already been
 * written to fd at that point.
 */
ssize_t WriteAll(SharedFD fd, const std::string& buf);

/**
 * Writes to fd until writing all bytes in buf.
 *
 * On a successful write, returns buf.size().
 *
 * If a write error is encountered, returns -1. Some data may have already been
 * written to fd at that point.
 */
ssize_t WriteAll(SharedFD fd, const std::vector<char>& buf);

} // namespace cvd
