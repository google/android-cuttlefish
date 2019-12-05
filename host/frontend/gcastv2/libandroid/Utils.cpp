/*
 * Copyright (C) 2009 The Android Open Source Project
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

#include <arpa/inet.h>

#include <media/stagefright/Utils.h>

namespace android {

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

// XXX warning: these won't work on big-endian host.
uint64_t ntoh64(uint64_t x) {
    return ((uint64_t)ntohl(x & 0xffffffff) << 32) | ntohl(x >> 32);
}

uint64_t hton64(uint64_t x) {
    return ((uint64_t)htonl(x & 0xffffffff) << 32) | htonl(x >> 32);
}

std::string MakeUserAgent() {
    return "stagefright/1.2 (OS X)";
}

void toLower(std::string *s) {
    std::transform(s->begin(), s->end(), s->begin(), ::tolower);
}

void trim(std::string *s) {
    size_t i = 0;
    while (i < s->size() && isspace(s->at(i))) {
        ++i;
    }

    size_t j = s->size();
    while (j > i && isspace(s->at(j - 1))) {
        --j;
    }

    s->erase(0, i);
    j -= i;
    s->erase(j);
}

bool startsWith(std::string_view s1, std::string_view s2) {
    if (s1.size() < s2.size()) {
        return false;
    }

    return s1.substr(0, s2.size()) == s2;
}

std::string StringPrintf(const char *format, ...) {
    va_list ap;
    va_start(ap, format);

    char *buffer;
    (void)vasprintf(&buffer, format, ap);

    va_end(ap);

    std::string result(buffer);

    free(buffer);
    buffer = NULL;

    return result;
}

}  // namespace android

