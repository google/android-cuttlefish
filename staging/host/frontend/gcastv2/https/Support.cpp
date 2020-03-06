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
#include <sys/errno.h>

void makeFdNonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) { flags = 0; }
    DEBUG_ONLY(int res = )fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    assert(res >= 0);
}

void hexdump(const void *_data, size_t size) {
  const uint8_t *data = static_cast<const uint8_t *>(_data);

  fprintf(stderr, "\n");

  size_t offset = 0;
  while (offset < size) {
    fprintf(stderr, "%08zx: ", offset);

    for (size_t col = 0; col < 16; ++col) {
      if (offset + col < size) {
        fprintf(stderr, "%02x ", data[offset + col]);
      } else {
        fprintf(stderr, "   ");
      }

      if (col == 7) {
        fprintf(stderr, " ");
      }
    }

    fprintf(stderr, " ");

    for (size_t col = 0; col < 16; ++col) {
      if (offset + col < size && isprint(data[offset + col])) {
        fprintf(stderr, "%c", data[offset + col]);
      } else if (offset + col < size) {
        fprintf(stderr, ".");
      }
    }

    fprintf(stderr, "\n");

    offset += 16;
  }

  fprintf(stderr, "\n");

}

static char encode6Bit(unsigned x) {
  static char base64[] =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  return base64[x & 63];
}

void encodeBase64(const void *_data, size_t size, std::string *out) {
    out->clear();
    out->reserve(((size+2)/3)*4);

    const uint8_t *data = (const uint8_t *)_data;

    size_t i;
    for (i = 0; i < (size / 3) * 3; i += 3) {
        uint8_t x1 = data[i];
        uint8_t x2 = data[i + 1];
        uint8_t x3 = data[i + 2];

        out->append(1, encode6Bit(x1 >> 2));
        out->append(1, encode6Bit((x1 << 4 | x2 >> 4) & 0x3f));
        out->append(1, encode6Bit((x2 << 2 | x3 >> 6) & 0x3f));
        out->append(1, encode6Bit(x3 & 0x3f));
    }
    switch (size % 3) {
        case 0:
            break;
        case 2:
        {
            uint8_t x1 = data[i];
            uint8_t x2 = data[i + 1];
            out->append(1, encode6Bit(x1 >> 2));
            out->append(1, encode6Bit((x1 << 4 | x2 >> 4) & 0x3f));
            out->append(1, encode6Bit((x2 << 2) & 0x3f));
            out->append(1, '=');
            break;
        }
        default:
        {
            uint8_t x1 = data[i];
            out->append(1, encode6Bit(x1 >> 2));
            out->append(1, encode6Bit((x1 << 4) & 0x3f));
            out->append("==");
            break;
        }
    }
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
