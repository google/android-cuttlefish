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

#include <webrtc/SCTPHandler.h>

#include "Utils.h"

#include <https/SafeCallbackable.h>
#include <https/Support.h>

#include <android-base/logging.h>

SCTPHandler::SCTPHandler(
        std::shared_ptr<RunLoop> runLoop,
        std::shared_ptr<DTLS> dtls)
    : mRunLoop(runLoop),
      mDTLS(dtls),
      mInitiateTag(0),
      mSendingTSN(0) {
}

void SCTPHandler::run() {
}

int SCTPHandler::inject(uint8_t *data, size_t size) {
    LOG(VERBOSE) << "Received SCTP datagram of size " << size << ":";
    LOG(VERBOSE) << hexdump(data, size);

    if (size < 12) {
        // Need at least the common header.
        return -EINVAL;
    }

    auto srcPort = U16_AT(&data[0]);
    auto dstPort = U16_AT(&data[2]);

    if (dstPort != 5000) {
        return -EINVAL;
    }

    auto checkSumIn = U32_AT(&data[8]);
    SET_U32(&data[8], 0x00000000);
    auto checkSum = crc32c(data, size);

    if (checkSumIn != checkSum) {
        LOG(WARNING)
            << "SCTPHandler::inject checksum invalid."
            << " (in: " << StringPrintf("0x%08x", checkSumIn) << ", "
            << "computed: " << StringPrintf("0x%08x", checkSum) << ")";

        return -EINVAL;
    }

    bool firstChunk = true;
    size_t offset = 12;
    while (offset < size) {
        if (offset + 4 > size) {
            return -EINVAL;
        }

        size_t chunkLength = U16_AT(&data[offset + 2]);

        if (offset + chunkLength > size) {
            return -EINVAL;
        }

        size_t paddedChunkLength = chunkLength;
        size_t pad = chunkLength % 4;
        if (pad) {
            pad = 4 - pad;
            paddedChunkLength += pad;
        }

        bool lastChunk =
            (offset + chunkLength == size)
                || (offset + paddedChunkLength == size);

        auto err = processChunk(
                srcPort,
                &data[offset],
                chunkLength,
                firstChunk,
                lastChunk);

        if (err) {
            return err;
        }

        firstChunk = false;

        offset += chunkLength;

        if (offset == size) {
            break;
        }

        if (offset + pad > size) {
            return -EINVAL;
        }

        offset += pad;
    }

    return 0;
}

void SCTPHandler::onDataChannel(
        const std::string &channel_label,
        std::function<void(std::shared_ptr<DataChannelStream>)> cb) {
    on_data_channel_callbacks_[channel_label] = cb;
    for (auto stream_it : this->streams_) {
        auto stream = stream_it.second;
        if (stream->IsDataChannel()) {
            auto data_channel = std::static_pointer_cast<DataChannelStream>(stream);
            if (data_channel->label() == channel_label) {
                cb(data_channel);
            }
        }
    }
}

int SCTPHandler::processChunk(
        uint16_t srcPort,
        const uint8_t *data,
        size_t size,
        bool firstChunk,
        bool lastChunk) {
    static constexpr uint8_t DATA = 0;
    static constexpr uint8_t INIT = 1;
    static constexpr uint8_t INIT_ACK = 2;
    static constexpr uint8_t SACK = 3;
    static constexpr uint8_t HEARTBEAT = 4;
    static constexpr uint8_t HEARTBEAT_ACK = 5;
    static constexpr uint8_t COOKIE_ECHO = 10;
    static constexpr uint8_t COOKIE_ACK = 11;
    static constexpr uint8_t SHUTDOWN_COMPLETE = 14;

    static constexpr uint64_t kCookie = 0xDABBAD00DEADBAADull;

    auto chunkType = data[0];
    if ((!firstChunk || !lastChunk)
            && (chunkType == INIT
                    || chunkType == INIT_ACK
                    || chunkType == SHUTDOWN_COMPLETE)) {
        // These chunks must be by themselves, no other chunks must be part
        // of the same datagram.

        return -EINVAL;
    }

    switch (chunkType) {
        case INIT:
        {
            if (size < 20) {
                return -EINVAL;
            }

            mInitiateTag = U32_AT(&data[4]);

            uint8_t out[12 + 24 + sizeof(kCookie)];
            SET_U16(&out[0], 5000);
            SET_U16(&out[2], srcPort);
            SET_U32(&out[4], mInitiateTag);
            SET_U32(&out[8], 0x00000000);  // Checksum: to be filled in below.

            size_t offset = 12;
            out[offset++] = INIT_ACK;
            out[offset++] = 0x00;

            SET_U16(&out[offset], sizeof(out) - 12);
            offset += 2;

            SET_U32(&out[offset], 0xb0b0cafe);  // initiate tag
            offset += 4;

            SET_U32(&out[offset], 0x00020000);  // a_rwnd
            offset += 4;

            SET_U16(&out[offset], 1);  // Number of Outbound Streams
            offset += 2;

            SET_U16(&out[offset], 1);  // Number of Inbound Streams
            offset += 2;

            mSendingTSN = 0x12345678;

            SET_U32(&out[offset], mSendingTSN);  // Initial TSN
            offset += 4;

            SET_U16(&out[offset], 0x0007);  // STATE_COOKIE
            offset += 2;

            static_assert((sizeof(kCookie) % 4) == 0);

            SET_U16(&out[offset], 4 + sizeof(kCookie));
            offset += 2;

            memcpy(&out[offset], &kCookie, sizeof(kCookie));
            offset += sizeof(kCookie);

            CHECK_EQ(offset, sizeof(out));

            SET_U32(&out[8], crc32c(out, sizeof(out)));

            LOG(VERBOSE) << "Sending SCTP INIT_ACK:";
            LOG(VERBOSE) << hexdump(out, sizeof(out));

            mDTLS->writeApplicationData(out, sizeof(out));
            break;
        }

        case COOKIE_ECHO:
        {
            if (size != (4 + sizeof(kCookie))) {
                return -EINVAL;
            }

            if (memcmp(&data[4], &kCookie, sizeof(kCookie))) {
                return -EINVAL;
            }

            uint8_t out[12 + 4];
            SET_U16(&out[0], 5000);
            SET_U16(&out[2], srcPort);
            SET_U32(&out[4], mInitiateTag);
            SET_U32(&out[8], 0x00000000);  // Checksum: to be filled in below.

            size_t offset = 12;
            out[offset++] = COOKIE_ACK;
            out[offset++] = 0x00;
            SET_U16(&out[offset], sizeof(out) - 12);
            offset += 2;

            CHECK_EQ(offset, sizeof(out));

            SET_U32(&out[8], crc32c(out, sizeof(out)));

            LOG(VERBOSE) << "Sending SCTP COOKIE_ACK:";
            LOG(VERBOSE) << hexdump(out, sizeof(out));

            mDTLS->writeApplicationData(out, sizeof(out));
            break;
        }

        case DATA:
        {
            if (size < 17) {
                // Minimal size (16 bytes header + 1 byte payload), empty
                // payloads are prohibited.
                return -EINVAL;
            }

            auto stream_id = U16_AT(&data[8]);
            auto stream_sn = U16_AT(&data[10]);
            if (streams_.count(stream_id) == 0) {
                if (stream_sn != 0) {
                    LOG(ERROR) << "Received non-first sequence number ("
                            << stream_sn << ") of previously unknown stream ("
                            << stream_id << ")";
                    break;
                }
                auto stream = streams_[stream_id] = std::shared_ptr<SCTPStream>(
                    SCTPStream::CreateStream(data, size));
                // Inject the first packet before checking the label!!!
                stream->InjectPacket(data, size);
                if (stream->IsDataChannel()) {
                    auto data_channel =
                        std::static_pointer_cast<DataChannelStream>(stream);
                    auto label = data_channel->label();
                    if (on_data_channel_callbacks_.count(label)) {
                        on_data_channel_callbacks_[label](data_channel);
                    }
                }
            } else {
                streams_[stream_id]->InjectPacket(data, size);
            }

            auto TSN = U32_AT(&data[4]);

            uint8_t out[12 + 16];
            SET_U16(&out[0], 5000);
            SET_U16(&out[2], srcPort);
            SET_U32(&out[4], mInitiateTag);
            SET_U32(&out[8], 0x00000000);  // Checksum: to be filled in below.

            size_t offset = 12;
            out[offset++] = SACK;
            out[offset++] = 0x00;

            SET_U16(&out[offset], sizeof(out) - 12);
            offset += 2;

            SET_U32(&out[offset], TSN);
            offset += 4;

            SET_U32(&out[offset], 0x00020000);  // a_rwnd
            offset += 4;

            SET_U16(&out[offset], 0);  // Number of Gap Ack Blocks
            offset += 2;

            SET_U16(&out[offset], 0);  // Number of Duplicate TSNs
            offset += 2;

            CHECK_EQ(offset, sizeof(out));

            SET_U32(&out[8], crc32c(out, sizeof(out)));

            LOG(VERBOSE) << "Sending SCTP SACK:";
            LOG(VERBOSE) << hexdump(out, sizeof(out));

            mDTLS->writeApplicationData(out, sizeof(out));
            break;
        }

        case HEARTBEAT:
        {
            if (size < 8) {
                return -EINVAL;
            }

            if (U16_AT(&data[4]) != 1 /* Heartbeat Info Type */
                || size != (U16_AT(&data[6]) + 4)) {
                return -EINVAL;
            }

            size_t pad = size % 4;
            if (pad) {
                pad = 4 - pad;
            }

            std::vector<uint8_t> outVec(12 + size + pad);

            uint8_t *out = outVec.data();
            SET_U16(&out[0], 5000);
            SET_U16(&out[2], srcPort);
            SET_U32(&out[4], mInitiateTag);
            SET_U32(&out[8], 0x00000000);  // Checksum: to be filled in below.

            size_t offset = 12;
            out[offset++] = HEARTBEAT_ACK;
            out[offset++] = 0x00;

            SET_U16(&out[offset], outVec.size() - 12 - pad);
            offset += 2;

            memcpy(&out[offset], &data[4], size - 4);
            offset += size - 4;

            memset(&out[offset], 0x00, pad);
            offset += pad;

            CHECK_EQ(offset, outVec.size());

            SET_U32(&out[8], crc32c(out, outVec.size()));

            LOG(VERBOSE) << "Sending SCTP HEARTBEAT_ACK:";
            LOG(VERBOSE) << hexdump(out, outVec.size());

            mDTLS->writeApplicationData(out, outVec.size());
            break;
        }

        default:
            break;
    }

    return 0;
}

static const uint32_t crc_c[256] = {
    0x00000000, 0xF26B8303, 0xE13B70F7, 0x1350F3F4,
    0xC79A971F, 0x35F1141C, 0x26A1E7E8, 0xD4CA64EB,
    0x8AD958CF, 0x78B2DBCC, 0x6BE22838, 0x9989AB3B,
    0x4D43CFD0, 0xBF284CD3, 0xAC78BF27, 0x5E133C24,
    0x105EC76F, 0xE235446C, 0xF165B798, 0x030E349B,
    0xD7C45070, 0x25AFD373, 0x36FF2087, 0xC494A384,
    0x9A879FA0, 0x68EC1CA3, 0x7BBCEF57, 0x89D76C54,
    0x5D1D08BF, 0xAF768BBC, 0xBC267848, 0x4E4DFB4B,
    0x20BD8EDE, 0xD2D60DDD, 0xC186FE29, 0x33ED7D2A,
    0xE72719C1, 0x154C9AC2, 0x061C6936, 0xF477EA35,
    0xAA64D611, 0x580F5512, 0x4B5FA6E6, 0xB93425E5,
    0x6DFE410E, 0x9F95C20D, 0x8CC531F9, 0x7EAEB2FA,
    0x30E349B1, 0xC288CAB2, 0xD1D83946, 0x23B3BA45,
    0xF779DEAE, 0x05125DAD, 0x1642AE59, 0xE4292D5A,
    0xBA3A117E, 0x4851927D, 0x5B016189, 0xA96AE28A,
    0x7DA08661, 0x8FCB0562, 0x9C9BF696, 0x6EF07595,
    0x417B1DBC, 0xB3109EBF, 0xA0406D4B, 0x522BEE48,
    0x86E18AA3, 0x748A09A0, 0x67DAFA54, 0x95B17957,
    0xCBA24573, 0x39C9C670, 0x2A993584, 0xD8F2B687,
    0x0C38D26C, 0xFE53516F, 0xED03A29B, 0x1F682198,
    0x5125DAD3, 0xA34E59D0, 0xB01EAA24, 0x42752927,
    0x96BF4DCC, 0x64D4CECF, 0x77843D3B, 0x85EFBE38,
    0xDBFC821C, 0x2997011F, 0x3AC7F2EB, 0xC8AC71E8,
    0x1C661503, 0xEE0D9600, 0xFD5D65F4, 0x0F36E6F7,
    0x61C69362, 0x93AD1061, 0x80FDE395, 0x72966096,
    0xA65C047D, 0x5437877E, 0x4767748A, 0xB50CF789,
    0xEB1FCBAD, 0x197448AE, 0x0A24BB5A, 0xF84F3859,
    0x2C855CB2, 0xDEEEDFB1, 0xCDBE2C45, 0x3FD5AF46,
    0x7198540D, 0x83F3D70E, 0x90A324FA, 0x62C8A7F9,
    0xB602C312, 0x44694011, 0x5739B3E5, 0xA55230E6,
    0xFB410CC2, 0x092A8FC1, 0x1A7A7C35, 0xE811FF36,
    0x3CDB9BDD, 0xCEB018DE, 0xDDE0EB2A, 0x2F8B6829,
    0x82F63B78, 0x709DB87B, 0x63CD4B8F, 0x91A6C88C,
    0x456CAC67, 0xB7072F64, 0xA457DC90, 0x563C5F93,
    0x082F63B7, 0xFA44E0B4, 0xE9141340, 0x1B7F9043,
    0xCFB5F4A8, 0x3DDE77AB, 0x2E8E845F, 0xDCE5075C,
    0x92A8FC17, 0x60C37F14, 0x73938CE0, 0x81F80FE3,
    0x55326B08, 0xA759E80B, 0xB4091BFF, 0x466298FC,
    0x1871A4D8, 0xEA1A27DB, 0xF94AD42F, 0x0B21572C,
    0xDFEB33C7, 0x2D80B0C4, 0x3ED04330, 0xCCBBC033,
    0xA24BB5A6, 0x502036A5, 0x4370C551, 0xB11B4652,
    0x65D122B9, 0x97BAA1BA, 0x84EA524E, 0x7681D14D,
    0x2892ED69, 0xDAF96E6A, 0xC9A99D9E, 0x3BC21E9D,
    0xEF087A76, 0x1D63F975, 0x0E330A81, 0xFC588982,
    0xB21572C9, 0x407EF1CA, 0x532E023E, 0xA145813D,
    0x758FE5D6, 0x87E466D5, 0x94B49521, 0x66DF1622,
    0x38CC2A06, 0xCAA7A905, 0xD9F75AF1, 0x2B9CD9F2,
    0xFF56BD19, 0x0D3D3E1A, 0x1E6DCDEE, 0xEC064EED,
    0xC38D26C4, 0x31E6A5C7, 0x22B65633, 0xD0DDD530,
    0x0417B1DB, 0xF67C32D8, 0xE52CC12C, 0x1747422F,
    0x49547E0B, 0xBB3FFD08, 0xA86F0EFC, 0x5A048DFF,
    0x8ECEE914, 0x7CA56A17, 0x6FF599E3, 0x9D9E1AE0,
    0xD3D3E1AB, 0x21B862A8, 0x32E8915C, 0xC083125F,
    0x144976B4, 0xE622F5B7, 0xF5720643, 0x07198540,
    0x590AB964, 0xAB613A67, 0xB831C993, 0x4A5A4A90,
    0x9E902E7B, 0x6CFBAD78, 0x7FAB5E8C, 0x8DC0DD8F,
    0xE330A81A, 0x115B2B19, 0x020BD8ED, 0xF0605BEE,
    0x24AA3F05, 0xD6C1BC06, 0xC5914FF2, 0x37FACCF1,
    0x69E9F0D5, 0x9B8273D6, 0x88D28022, 0x7AB90321,
    0xAE7367CA, 0x5C18E4C9, 0x4F48173D, 0xBD23943E,
    0xF36E6F75, 0x0105EC76, 0x12551F82, 0xE03E9C81,
    0x34F4F86A, 0xC69F7B69, 0xD5CF889D, 0x27A40B9E,
    0x79B737BA, 0x8BDCB4B9, 0x988C474D, 0x6AE7C44E,
    0xBE2DA0A5, 0x4C4623A6, 0x5F16D052, 0xAD7D5351,
};

#define CRC32C_POLY 0x1EDC6F41
#define CRC32C(c,d) (c=(c>>8)^crc_c[(c^(d))&0xFF])

static uint32_t swap32(uint32_t x) {
    return (x >> 24)
        | (((x >> 16) & 0xff) << 8)
        | (((x >> 8) & 0xff) << 16)
        | ((x & 0xff) << 24);
}

// static
uint32_t SCTPHandler::crc32c(const uint8_t *data, size_t size) {
    uint32_t crc32 = ~(uint32_t)0;

    for (size_t i = 0; i < size; ++i) {
        CRC32C(crc32, data[i]);
    }

    return ~swap32(crc32);
}

