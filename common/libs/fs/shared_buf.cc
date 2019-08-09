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

namespace cvd {

namespace {

const size_t BUFF_SIZE = 1 << 14;

} // namespace

ssize_t ReadAll(SharedFD fd, std::string* buf) {
  char buff[BUFF_SIZE];
  std::stringstream ss;
  ssize_t read;
  while ((read = fd->Read(buff, BUFF_SIZE - 1)) > 0) {
    // this is necessary to avoid problems with having a '\0' in the middle of the buffer
    ss << std::string(buff, read);
  }
  if (read < 0) {
    return read;
  }
  *buf = ss.str();
  return buf->size();
}

} // namespace cvd
