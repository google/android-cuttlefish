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

#include <webrtc/RTPSender.h>

#include "Utils.h"

#include <webrtc/RTPSocketHandler.h>

#include <https/SafeCallbackable.h>
#include <https/Support.h>

#include <random>
#include <unordered_set>

#define SIMULATE_PACKET_LOSS    0

RTPSender::RTPSender(
        std::shared_ptr<RunLoop> runLoop,
        RTPSocketHandler *parent,
        std::shared_ptr<Packetizer> videoPacketizer,
        std::shared_ptr<Packetizer> audioPacketizer)
    : mRunLoop(runLoop),
      mParent(parent),
      mVideoPacketizer(videoPacketizer),
      mAudioPacketizer(audioPacketizer) {
}

void RTPSender::addSource(uint32_t ssrc) {
    CHECK(mSources.insert(
                std::make_pair(ssrc, SourceInfo())).second);
}

void RTPSender::addRetransInfo(
        uint32_t ssrc, uint8_t PT, uint32_t retransSSRC, uint8_t retransPT) {
    auto it = mSources.find(ssrc);
    CHECK(it != mSources.end());

    auto &info = it->second;

    CHECK(info.mRetrans.insert(
                std::make_pair(
                    PT, std::make_pair(retransSSRC, retransPT))).second);
}

int RTPSender::injectRTCP(uint8_t *data, size_t size) {
    LOG(VERBOSE) << "RTPSender::injectRTCP";
    LOG(VERBOSE) << hexdump(data, size);

    while (size > 0) {
        if (size < 8) {
            return -EINVAL;
        }

        if ((data[0] >> 6) != 2) {
            // Wrong version.
            return -EINVAL;
        }

        size_t lengthInWords = U16_AT(&data[2]) + 1;

        bool hasPadding = (data[0] & 0x20);

        size_t headerSize = 4 * lengthInWords;

        if (size < headerSize) {
            return -EINVAL;
        }

        if (hasPadding) {
            if (size != headerSize) {
                // Padding should only be added to the last packet in a compound
                // packet.
                return -EINVAL;
            }

            size_t numPadBytes = data[headerSize - 1];
            if (numPadBytes == 0 || (numPadBytes % 4) != 0) {
                return -EINVAL;
            }

            headerSize -= numPadBytes;
        }

        auto err = processRTCP(data, headerSize);

        if (err) {
            return err;
        }

        data += 4 * lengthInWords;
        size -= 4 * lengthInWords;
    }

    return 0;
}

int RTPSender::processRTCP(const uint8_t *data, size_t size) {
    static constexpr uint8_t RR = 201;     // RFC 3550
    // static constexpr uint8_t SDES = 202;
    // static constexpr uint8_t BYE = 203;
    // static constexpr uint8_t APP = 204;
    static constexpr uint8_t RTPFB = 205;  // RFC 4585
    static constexpr uint8_t PSFB = 206;
    static constexpr uint8_t XR = 207;  // RFC 3611

    unsigned PT = data[1];

    switch (PT) {
        case RR:
        {
            unsigned RC = data[0] & 0x1f;
            if (size != 8 + RC * 6 * 4) {
                return -EINVAL;
            }

            auto senderSSRC = U32_AT(&data[4]);

            size_t offset = 8;
            for (unsigned i = 0; i < RC; ++i) {
                auto SSRC = U32_AT(&data[offset]);
                auto fractionLost = data[offset + 4];
                auto cumPacketsLost = U32_AT(&data[offset + 4]) & 0xffffff;

                if (fractionLost) {
                    LOG(INFO)
                        << "sender SSRC "
                        << StringPrintf("0x%08x", senderSSRC)
                        << " reports "
                        << StringPrintf("%.2f %%", (double)fractionLost * 100.0 / 256.0)
                        << " lost, cum. total: "
                        << cumPacketsLost
                        << " from SSRC "
                        << StringPrintf("0x%08x", SSRC);
                }

                offset += 6 * 4;
            }
            break;
        }

        case RTPFB:
        {
            static constexpr uint8_t NACK = 1;

            if (size < 12) {
                return -EINVAL;
            }

            unsigned fmt = data[0] & 0x1f;

            auto senderSSRC = U32_AT(&data[4]);
            auto SSRC = U32_AT(&data[8]);

            switch (fmt) {
                case NACK:
                {
                    size_t offset = 12;
                    size_t n = (size - offset) / 4;
                    for (size_t i = 0; i < n; ++i) {
                        auto PID = U16_AT(&data[offset]);
                        auto BLP = U16_AT(&data[offset + 2]);

                        LOG(INFO)
                            << "SSRC "
                            << StringPrintf("0x%08x", senderSSRC)
                            << " reports NACK w/ PID="
                            << StringPrintf("0x%04x", PID)
                            << ", BLP="
                            << StringPrintf("0x%04x", BLP)
                            << " from SSRC "
                            << StringPrintf("0x%08x", SSRC);

                        offset += 4;

                        retransmitPackets(SSRC, PID, BLP);
                    }
                    break;
                }

                default:
                {
                    LOG(WARNING) << "RTPSender::processRTCP unhandled RTPFB.";
                    LOG(INFO) << hexdump(data, size);
                    break;
                }
            }

            break;
        }

        case PSFB:
        {
            static constexpr uint8_t FMT_PLI = 1;
            static constexpr uint8_t FMT_SLI = 2;
            static constexpr uint8_t FMT_AFB = 15;

            if (size < 12) {
                return -EINVAL;
            }

            unsigned fmt = data[0] & 0x1f;

            auto SSRC = U32_AT(&data[4]);

            switch (fmt) {
                case FMT_PLI:
                {
                    if (size != 12) {
                        return -EINVAL;
                    }

                    LOG(INFO)
                        << "Received PLI from SSRC "
                        << StringPrintf("0x%08x", SSRC);

                    if (mVideoPacketizer) {
                        mVideoPacketizer->requestIDRFrame();
                    }
                    break;
                }

                case FMT_SLI:
                {
                    LOG(INFO)
                        << "Received SLI from SSRC "
                        << StringPrintf("0x%08x", SSRC);

                    break;
                }

                case FMT_AFB:
                    break;

                default:
                {
                    LOG(WARNING) << "RTPSender::processRTCP unhandled PSFB.";
                    LOG(INFO) << hexdump(data, size);
                    break;
                }
            }
            break;
        }

        case XR:
        {
            static constexpr uint8_t FMT_RRTRB = 4;

            if (size < 8) {
                return -EINVAL;
            }

            auto senderSSRC = U32_AT(&data[4]);

            size_t offset = 8;
            while (offset + 3 < size) {
                auto fmt = data[offset];
                auto blockLength = 4 * (1 + U16_AT(&data[offset + 2]));

                if (offset + blockLength > size) {
                    LOG(WARNING) << "Found incomplete XR report block.";
                    break;
                }

                switch (fmt) {
                    case FMT_RRTRB:
                    {
                        if (blockLength != 12) {
                            LOG(WARNING)
                                << "Found XR-RRTRB block of invalid length.";
                            break;
                        }

                        auto ntpHi = U32_AT(&data[offset + 4]);
                        auto ntpLo = U32_AT(&data[offset + 8]);

                        queueDLRR(
                                0xdeadbeef /* localSSRC */,
                                senderSSRC,
                                ntpHi,
                                ntpLo);
                        break;
                    }

                    default:
                    {
                        LOG(WARNING)
                            << "Ignoring unknown XR block type " << fmt;

                        break;
                    }
                }

                offset += blockLength;
            }

            if (offset != size) {
                LOG(WARNING) << "Found trailing bytes in XR report.";
            }
            break;
        }

        default:
        {
            LOG(WARNING) << "RTPSender::processRTCP unhandled packet type.";
            LOG(INFO) << hexdump(data, size);
        }
    }

    return 0;
}

void RTPSender::appendSR(std::vector<uint8_t> *buffer, uint32_t localSSRC) {
    static constexpr uint8_t SR = 200;

    auto it = mSources.find(localSSRC);
    CHECK(it != mSources.end());

    const auto &info = it->second;

    const size_t kLengthInWords = 7;

    auto offset = buffer->size();
    buffer->resize(offset + kLengthInWords * sizeof(uint32_t));

    uint8_t *data = buffer->data() + offset;

    data[0] = 0x80;
    data[1] = SR;
    SET_U16(&data[2], kLengthInWords - 1);
    SET_U32(&data[4], localSSRC);

    auto now = std::chrono::system_clock::now();

    auto us_since_epoch =
        std::chrono::duration_cast<std::chrono::microseconds>(
            now.time_since_epoch()).count();

    // This assumes that sd::chrono::system_clock's epoch is unix epoch, i.e.
    // 1/1/1970 midnight UTC.
    // Microseconds between midnight 1/1/1970 and midnight 1/1/1900.
    us_since_epoch += 2208988800ULL * 1000ull;

    uint64_t ntpHi = us_since_epoch / 1000000ll;
    uint64_t ntpLo = ((1LL << 32) * (us_since_epoch % 1000000LL)) / 1000000LL;

    uint32_t rtpNow =
        (localSSRC == 0xdeadbeef || localSSRC == 0xcafeb0b0)
            ? mVideoPacketizer->rtpNow()
            : mAudioPacketizer->rtpNow();

    SET_U32(&data[8], ntpHi);
    SET_U32(&data[12], ntpLo);
    SET_U32(&data[16], rtpNow);
    SET_U32(&data[20], info.mNumPacketsSent);
    SET_U32(&data[24], info.mNumBytesSent);
}

void RTPSender::appendSDES(std::vector<uint8_t> *buffer, uint32_t localSSRC) {
    static constexpr uint8_t SDES = 202;

    static const char *const kCNAME = "myWebRTP";
    static const size_t kCNAMELength = strlen(kCNAME);

    const size_t kLengthInWords = 2 + (2 + kCNAMELength + 1 + 3) / 4;

    auto offset = buffer->size();
    buffer->resize(offset + kLengthInWords * sizeof(uint32_t));

    uint8_t *data = buffer->data() + offset;

    data[0] = 0x81;
    data[1] = SDES;
    SET_U16(&data[2], kLengthInWords - 1);
    SET_U32(&data[4], localSSRC);

    data[8] = 1; // CNAME
    data[9] = kCNAMELength;
    memcpy(&data[10], kCNAME, kCNAMELength);
    data[10 + kCNAMELength] = '\0';
}

void RTPSender::queueDLRR(
        uint32_t localSSRC,
        uint32_t remoteSSRC,
        uint32_t ntpHi,
        uint32_t ntpLo) {
    std::vector<uint8_t> buffer;
    appendDLRR(&buffer, localSSRC, remoteSSRC, ntpHi, ntpLo);

    mParent->queueRTCPDatagram(buffer.data(), buffer.size());
}

void RTPSender::appendDLRR(
        std::vector<uint8_t> *buffer,
        uint32_t localSSRC,
        uint32_t remoteSSRC,
        uint32_t ntpHi,
        uint32_t ntpLo) {
    static constexpr uint8_t XR = 207;

    static constexpr uint8_t FMT_DLRRRB = 5;

    const size_t kLengthInWords = 2 + 4;

    auto offset = buffer->size();
    buffer->resize(offset + kLengthInWords * sizeof(uint32_t));

    uint8_t *data = buffer->data() + offset;

    data[0] = 0x80;
    data[1] = XR;
    SET_U16(&data[2], kLengthInWords - 1);
    SET_U32(&data[4], localSSRC);

    data[8] = FMT_DLRRRB;
    data[9] = 0x00;
    SET_U16(&data[10], 3 /* block length */);
    SET_U32(&data[12], remoteSSRC);
    SET_U32(&data[16], (ntpHi << 16) | (ntpLo >> 16));
    SET_U32(&data[20], 0 /* delay since last RR */);
}

void RTPSender::queueSR(uint32_t localSSRC) {
    std::vector<uint8_t> buffer;
    appendSR(&buffer, localSSRC);
    // appendSDES(&buffer, localSSRC);

    LOG(VERBOSE) << "RTPSender::queueSR";
    LOG(VERBOSE) << hexdump(buffer.data(), buffer.size());

    mParent->queueRTCPDatagram(buffer.data(), buffer.size());
}

void RTPSender::sendSR(uint32_t localSSRC) {
    LOG(VERBOSE) << "sending SR.";
    queueSR(localSSRC);

    mRunLoop->postWithDelay(
            std::chrono::seconds(1),
            makeSafeCallback(this, &RTPSender::sendSR, localSSRC));
}

void RTPSender::run() {
    for (const auto &entry : mSources) {
        sendSR(entry.first);
    }
}

void RTPSender::queueRTPDatagram(std::vector<uint8_t> *packet) {
    CHECK_GE(packet->size(), 12u);

    uint32_t SSRC = U32_AT(&packet->data()[8]);

    auto it = mSources.find(SSRC);
    CHECK(it != mSources.end());

    auto &info = it->second;

    uint16_t seqNum = info.mNumPacketsSent;
    SET_U16(packet->data() + 2, seqNum);

#if SIMULATE_PACKET_LOSS
    static std::random_device rd;
    static std::mt19937 gen(rd());
    static std::uniform_real_distribution<> dist(0.0, 1.0);
    if (dist(gen) < 0.99) {
#endif
        mParent->queueRTPDatagram(packet->data(), packet->size());
#if SIMULATE_PACKET_LOSS
    } else {
        LOG(WARNING)
            << "dropping packet "
            << StringPrintf("0x%04x", seqNum)
            << " from SSRC "
            << StringPrintf("0x%08x", SSRC);
    }
#endif

    ++info.mNumPacketsSent;
    info.mNumBytesSent += packet->size() - 12;  // does not include RTP header.

    if (!info.mRetrans.empty()) {
        static constexpr size_t kMaxHistory = 512;
        if (info.mRecentPackets.size() == kMaxHistory) {
            info.mRecentPackets.pop_front();
        }
        // info.mRecentPackets.push_back(std::move(*packet));
        info.mRecentPackets.push_back(*packet);
    }
}

void RTPSender::retransmitPackets(
        uint32_t localSSRC, uint16_t PID, uint16_t BLP) {
    auto it = mSources.find(localSSRC);
    CHECK(it != mSources.end());

    const auto &info = it->second;

    if (!info.mRecentPackets.empty()) {
        LOG(INFO) << "Recent packets cover range ["
            << StringPrintf(
                    "0x%04x", U16_AT(info.mRecentPackets.front().data() + 2))
            << ";"
            << StringPrintf(
                    "0x%04x", U16_AT(info.mRecentPackets.back().data() + 2))
            << "]";
    } else {
        LOG(INFO) << "Recent packets are EMPTY!";
    }

    bool first = true;
    while (first || BLP) {
        if (first) {
            first = false;
        } else {
            ++PID;
            if (!(BLP & 1)) {
                BLP = BLP >> 1;
                continue;
            }

            BLP = BLP >> 1;
        }

        for (auto it = info.mRecentPackets.begin();
                it != info.mRecentPackets.end();
                ++it) {
            const auto &origPacket = *it;
            auto seqNum = U16_AT(origPacket.data() + 2);

            if (seqNum != PID) {
                continue;
            }

            LOG(INFO) << "Retransmitting PID " << StringPrintf("0x%04x", PID);

            auto PT = origPacket[1] & 0x7f;
            auto it2 = info.mRetrans.find(PT);
            CHECK(it2 != info.mRetrans.end());

            auto [rtxSSRC, rtxPT] = it2->second;

            std::vector<uint8_t> packet(origPacket.size() + 2);

            // XXX This is very simplified and assumes that the original packet
            // started with a standard 12-byte header, no extensions and no padding!
            memcpy(packet.data(), origPacket.data(), 12);

            packet[1] = (origPacket[1] & 0x80) | (rtxPT & 0x7f);
            SET_U32(packet.data() + 8, rtxSSRC);
            SET_U16(packet.data() + 12, seqNum);

            memcpy(packet.data() + 14,
                   origPacket.data() + 12,
                   origPacket.size() - 12);

            // queueRTPDatagram will fill in the new seqNum.
            queueRTPDatagram(&packet);
        }
    }
}

void RTPSender::requestIDRFrame() {
    if (mVideoPacketizer) {
        mVideoPacketizer->requestIDRFrame();
    }
}

