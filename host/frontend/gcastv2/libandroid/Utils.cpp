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

#include <media/stagefright/MetaData.h>

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

status_t convertMetaDataToMessage(
        const std::shared_ptr<MetaData> &meta, std::shared_ptr<AMessage> *format) {
    format->reset();

    const char *mime;
    CHECK(meta->findCString(kKeyMIMEType, &mime));

    std::shared_ptr<AMessage> msg(new AMessage);
    msg->setString("mime", mime);

    if (!strncasecmp("video/", mime, 6)) {
        int32_t width, height;
        CHECK(meta->findInt32(kKeyWidth, &width));
        CHECK(meta->findInt32(kKeyHeight, &height));

        msg->setInt32("width", width);
        msg->setInt32("height", height);
    } else if (!strncasecmp("audio/", mime, 6)) {
        int32_t numChannels, sampleRate;
        CHECK(meta->findInt32(kKeyChannelCount, &numChannels));
        CHECK(meta->findInt32(kKeySampleRate, &sampleRate));

        msg->setInt32("channel-count", numChannels);
        msg->setInt32("sample-rate", sampleRate);

        int32_t isADTS;
        if (meta->findInt32(kKeyIsADTS, &isADTS)) {
            msg->setInt32("is-adts", true);
        }
    }

    uint32_t type;
    const void *data;
    size_t size;
    if (meta->findData(kKeyAVCC, &type, &data, &size)) {
        // Parse the AVCDecoderConfigurationRecord

        const uint8_t *ptr = (const uint8_t *)data;

        CHECK(size >= 7);
        CHECK_EQ((unsigned)ptr[0], 1u);  // configurationVersion == 1
        // uint8_t profile = ptr[1];
        // uint8_t level = ptr[3];

        // There is decodable content out there that fails the following
        // assertion, let's be lenient for now...
        // CHECK((ptr[4] >> 2) == 0x3f);  // reserved

        // size_t lengthSize = 1 + (ptr[4] & 3);

        // commented out check below as H264_QVGA_500_NO_AUDIO.3gp
        // violates it...
        // CHECK((ptr[5] >> 5) == 7);  // reserved

        size_t numSeqParameterSets = ptr[5] & 31;

        ptr += 6;
        size -= 6;

        std::shared_ptr<ABuffer> buffer(new ABuffer(1024));
        buffer->setRange(0, 0);

        for (size_t i = 0; i < numSeqParameterSets; ++i) {
            CHECK(size >= 2);
            size_t length = U16_AT(ptr);

            ptr += 2;
            size -= 2;

            CHECK(size >= length);

            memcpy(buffer->data() + buffer->size(), "\x00\x00\x00\x01", 4);
            memcpy(buffer->data() + buffer->size() + 4, ptr, length);
            buffer->setRange(0, buffer->size() + 4 + length);

            ptr += length;
            size -= length;
        }

        buffer->meta()->setInt32("csd", true);
        buffer->meta()->setInt64("timeUs", 0);

        msg->setBuffer("csd-0", buffer);

        buffer.reset(new ABuffer(1024));
        buffer->setRange(0, 0);

        CHECK(size >= 1);
        size_t numPictureParameterSets = *ptr;
        ++ptr;
        --size;
        for (size_t i = 0; i < numPictureParameterSets; ++i) {
            CHECK(size >= 2);
            size_t length = U16_AT(ptr);

            ptr += 2;
            size -= 2;

            CHECK(size >= length);

            memcpy(buffer->data() + buffer->size(), "\x00\x00\x00\x01", 4);
            memcpy(buffer->data() + buffer->size() + 4, ptr, length);
            buffer->setRange(0, buffer->size() + 4 + length);

            ptr += length;
            size -= length;
        }

        buffer->meta()->setInt32("csd", true);
        buffer->meta()->setInt64("timeUs", 0);
        msg->setBuffer("csd-1", buffer);
    }

    *format = msg;

    return OK;
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

