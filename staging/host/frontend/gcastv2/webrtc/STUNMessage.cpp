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

#include <webrtc/STUNMessage.h>

#include "Utils.h"

#include <https/Support.h>
#include <android-base/logging.h>

#include <arpa/inet.h>

#include <cstring>
#include <iostream>
#include <unordered_map>

#include <openssl/hmac.h>

static constexpr uint8_t kMagicCookie[4] = { 0x21, 0x12, 0xa4, 0x42 };

STUNMessage::STUNMessage(uint16_t type, const uint8_t transactionID[12])
    : mIsValid(true),
      mData(20),
      mAddedMessageIntegrity(false) {
    CHECK((type >> 14) == 0);

    mData[0] = (type >> 8) & 0x3f;
    mData[1] = type & 0xff;
    mData[2] = 0;
    mData[3] = 0;

    memcpy(&mData[4], kMagicCookie, sizeof(kMagicCookie));

    memcpy(&mData[8], transactionID, 12);
}

STUNMessage::STUNMessage(const void *data, size_t size)
    : mIsValid(false),
      mData(size) {
    memcpy(mData.data(), data, size);

    validate();
}

bool STUNMessage::isValid() const {
    return mIsValid;
}

static uint16_t UINT16_AT(const void *_data) {
    const uint8_t *data = static_cast<const uint8_t *>(_data);
    return static_cast<uint16_t>(data[0]) << 8 | data[1];
}

uint16_t STUNMessage::type() const {
    return UINT16_AT(mData.data());
}

void STUNMessage::addAttribute(uint16_t type, const void *data, size_t size) {
    CHECK(!mAddedMessageIntegrity || type == 0x8028);

    size_t alignedSize = (size + 3) & ~3;
    CHECK_LE(alignedSize, 0xffffu);

    size_t offset = mData.size();
    mData.resize(mData.size() + 4 + alignedSize);

    uint8_t *ptr = mData.data() + offset;
    ptr[0] = type >> 8;
    ptr[1] = type & 0xff;
    ptr[2] = (size >> 8) & 0xff;
    ptr[3] = size & 0xff;

    if (size > 0) {
        memcpy(&ptr[4], data, size);
    }
}

void STUNMessage::addMessageIntegrityAttribute(std::string_view password) {
    size_t offset = mData.size();

    uint16_t truncatedLength = offset + 4;
    mData[2] = (truncatedLength >> 8);
    mData[3] = (truncatedLength & 0xff);

    uint8_t digest[20];
    unsigned int digestLen = sizeof(digest);

    HMAC(EVP_sha1(),
         password.data(),
         password.size(),
         mData.data(),
         offset,
         digest,
         &digestLen);

    CHECK_EQ(digestLen, 20);
    addAttribute(0x0008 /* MESSAGE-INTEGRITY */, digest, digestLen);

    mAddedMessageIntegrity = true;
}

const uint8_t *STUNMessage::data() {
    size_t size = mData.size() - 20;
    CHECK_LE(size, 0xffffu);

    mData[2] = (size >> 8) & 0xff;
    mData[3] = size & 0xff;

    return mData.data();
}

size_t STUNMessage::size() const {
    return mData.size();
}

void STUNMessage::validate() {
    if (mData.size() < 20) {
        return;
    }

    const uint8_t *data = mData.data();

    auto messageLength = UINT16_AT(data + 2);
    if (messageLength != mData.size() - 20) {
        return;
    }

    if (memcmp(kMagicCookie, &data[4], sizeof(kMagicCookie))) {
        return;
    }

    bool sawMessageIntegrity = false;

    data += 20;
    size_t offset = 0;
    while (offset + 4 <= messageLength) {
        auto attrType = UINT16_AT(&data[offset]);

        if (sawMessageIntegrity && attrType != 0x8028 /* FINGERPRINT */) {
            return;
        }

        sawMessageIntegrity = (attrType == 0x0008 /* MESSAGE-INTEGRITY */);

        auto attrLength = UINT16_AT(&data[offset + 2]);

        if (offset + 4 + attrLength > messageLength) {
            return;
        }

        offset += 4 + attrLength;
        if (offset & 3) {
            offset += 4 - (offset & 3);
        }
    }

    if (offset != messageLength) {
        return;
    }

    mAddedMessageIntegrity = sawMessageIntegrity;
    mIsValid = true;
}

void STUNMessage::dump(std::optional<std::string_view> password) const {
    CHECK(mIsValid);

    const uint8_t *data = mData.data();

    auto messageType = UINT16_AT(data);
    auto messageLength = mData.size() - 20;

    if (messageType == 0x0001) {
        std::cout << "Binding Request";
    } else if (messageType == 0x0101) {
        std::cout << "Binding Response";
    } else {
        std::cout
            << "Unknown message type "
            << StringPrintf("0x%04x", messageType);
    }

    std::cout << std::endl;

    data += 20;
    size_t offset = 0;
    while (offset + 4 <= messageLength) {
        auto attrType = UINT16_AT(&data[offset]);
        auto attrLength = UINT16_AT(&data[offset + 2]);

        static const std::unordered_map<uint16_t, std::string> kAttrName {
            { 0x0001, "MAPPED-ADDRESS" },
            { 0x0006, "USERNAME" },
            { 0x0008, "MESSAGE-INTEGRITY" },
            { 0x0009, "ERROR-CODE" },
            { 0x000A, "UNKNOWN-ATTRIBUTES" },
            { 0x0014, "REALM" },
            { 0x0015, "NONCE" },
            { 0x0020, "XOR-MAPPED-ADDRESS" },
            { 0x0024, "PRIORITY" },  // RFC8445
            { 0x0025, "USE-CANDIDATE" },  // RFC8445
            { 0x8022, "SOFTWARE" },
            { 0x8023, "ALTERNATE-SERVER" },
            { 0x8028, "FINGERPRINT" },
            { 0x8029, "ICE-CONTROLLED" },  // RFC8445
            { 0x802a, "ICE-CONTROLLING" },  // RFC8445
        };

        auto it = kAttrName.find(attrType);
        if (it == kAttrName.end()) {
            if (attrType <= 0x7fff) {
                std::cout
                    << "Unknown mandatory attribute type "
                    << StringPrintf("0x%04x", attrType)
                    << ":"
                    << std::endl;
            } else {
                std::cout
                    << "Unknown optional attribute type "
                    << StringPrintf("0x%04x", attrType)
                    << ":"
                    << std::endl;
            }
        } else {
            std::cout << "attribute '" << it->second << "':" << std::endl;
        }

        LOG(INFO) << hexdump(&data[offset + 4], attrLength);

        if (attrType == 8 /* MESSAGE_INTEGRITY */) {
            if (attrLength != 20) {
                LOG(WARNING)
                    << "Message integrity attribute length mismatch."
                    << " Expected 20, found "
                    << attrLength;
            } else if (password) {
                auto success = verifyMessageIntegrity(offset + 20, *password);

                if (!success) {
                    LOG(WARNING) << "Message integrity check FAILED!";
                }
            }
        } else if (attrType == 0x8028 /* FINGERPRINT */) {
            if (attrLength != 4) {
                LOG(WARNING)
                    << "Fingerprint attribute length mismatch."
                    << " Expected 4, found "
                    << attrLength;
            } else {
                auto success = verifyFingerprint(offset + 20);

                if (!success) {
                    LOG(WARNING) << "Fingerprint check FAILED!";
                }
            }
        }

        offset += 4 + attrLength;
        if (offset & 3) {
            offset += 4 - (offset & 3);
        }
    }
}

bool STUNMessage::verifyMessageIntegrity(
        size_t offset, std::string_view password) const {
    // Password used as "short-term" credentials (RFC 5389).
    // Technically the password would have to be SASLprep'ed...

    std::vector<uint8_t> copy(offset);
    memcpy(copy.data(), mData.data(), offset);

    uint16_t truncatedLength = offset + 4;
    copy[2] = (truncatedLength >> 8);
    copy[3] = (truncatedLength & 0xff);

    uint8_t digest[20];
    unsigned int digestLen = sizeof(digest);

    HMAC(EVP_sha1(),
         password.data(),
         password.size(),
         copy.data(),
         copy.size(),
         digest,
         &digestLen);

    CHECK_EQ(digestLen, 20);

    bool success = !memcmp(
            digest,
            &mData[offset + 4],
            digestLen);

    return success;
}

void STUNMessage::addFingerprint() {
    size_t offset = mData.size();

    // Pretend that we've added the FINGERPRINT attribute already.
    uint16_t truncatedLength = offset + 4 + 4 - 20;
    mData[2] = (truncatedLength >> 8);
    mData[3] = (truncatedLength & 0xff);

    uint32_t crc32 = htonl(computeCrc32(mData.data(), offset) ^ 0x5354554e);

    addAttribute(0x8028 /* FINGERPRINT */, &crc32, sizeof(crc32));
}

bool STUNMessage::verifyFingerprint(size_t offset) const {
    std::vector<uint8_t> copy(offset);
    memcpy(copy.data(), mData.data(), offset);

    copy[2] = ((mData.size() - 20) >> 8) & 0xff;
    copy[3] = (mData.size() - 20) & 0xff;

    uint32_t crc32 = htonl(computeCrc32(copy.data(), offset) ^ 0x5354554e);

    LOG(VERBOSE) << hexdump(&crc32, 4);

    return !memcmp(&crc32, &mData[offset + 4], 4);
}

bool STUNMessage::findAttribute(
        uint16_t type, const void **attrData, size_t *attrSize) const {
    CHECK(mIsValid);

    const uint8_t *data = mData.data();

    auto messageLength = mData.size() - 20;

    data += 20;
    size_t offset = 0;
    while (offset + 4 <= messageLength) {
        auto attrType = UINT16_AT(&data[offset]);
        auto attrLength = UINT16_AT(&data[offset + 2]);

        if (attrType == type) {
            *attrData = &data[offset + 4];
            *attrSize = attrLength;

            return true;
        }

        offset += 4 + attrLength;

        if (offset & 3) {
            offset += 4 - (offset & 3);
        }
    }

    *attrData = nullptr;
    *attrSize = 0;

    return false;
}

