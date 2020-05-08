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

#include <https/Support.h>

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <ctype.h>
#include <fcntl.h>
#include <iomanip>
#include <ios>
#include <iostream>
#include <sys/errno.h>
#include <sstream>

void makeFdNonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) { flags = 0; }
    DEBUG_ONLY(int res = )fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    assert(res >= 0);
}

std::string hexdump(const void* _data, size_t size) {
  std::stringstream ss;
  const uint8_t *data = static_cast<const uint8_t *>(_data);

  size_t offset = 0;
  while (offset < size) {
    ss << "0x" << std::hex << std::setw(8) << std::setfill('0') << offset << ": ";

    for (size_t col = 0; col < 16; ++col) {
      if (offset + col < size) {
        ss << std::setw(2) << static_cast<int>(data[offset + col]) << " ";
      } else {
        ss << "   ";
      }

      if (col == 7) {
        ss <<  " ";
      }
    }

    ss << " ";

    for (size_t col = 0; col < 16; ++col) {
      if (offset + col < size && isprint(data[offset + col])) {
        ss << static_cast<char>(data[offset + col]);
      } else if (offset + col < size) {
        ss << ".";
      }
    }

    ss << std::endl;

    offset += 16;
  }
  return ss.str();
}

uint16_t U16_AT(const uint8_t *ptr) {
    return ptr[0] << 8 | ptr[1];
}

uint32_t U32_AT(const uint8_t *ptr) {
    return ptr[0] << 24 | ptr[1] << 16 | ptr[2] << 8 | ptr[3];
}

uint64_t U64_AT(const uint8_t *ptr) {
    return ((uint64_t)U32_AT(ptr)) << 32 | U32_AT(ptr + 4);
}

uint16_t U16LE_AT(const uint8_t *ptr) {
    return ptr[0] | (ptr[1] << 8);
}

uint32_t U32LE_AT(const uint8_t *ptr) {
    return ptr[3] << 24 | ptr[2] << 16 | ptr[1] << 8 | ptr[0];
}

uint64_t U64LE_AT(const uint8_t *ptr) {
    return ((uint64_t)U32LE_AT(ptr + 4)) << 32 | U32LE_AT(ptr);
}

std::string STR_AT(const uint8_t *ptr, size_t size) {
  return std::string((const char*)ptr, size);
}
