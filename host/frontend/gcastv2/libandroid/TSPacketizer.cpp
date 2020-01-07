/*
 * Copyright 2012, The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

//#define LOG_NDEBUG 0
#define LOG_TAG "TSPacketizer"
#include <utils/Log.h>

#include <media/stagefright/foundation/TSPacketizer.h>

#include <media/stagefright/avc_utils.h>
#include <media/stagefright/Utils.h>
#include <media/stagefright/foundation/ABuffer.h>
#include <media/stagefright/foundation/ADebug.h>
#include <media/stagefright/foundation/AMessage.h>
#include <media/stagefright/foundation/hexdump.h>
#include <media/stagefright/MediaDefs.h>
#include <media/stagefright/MediaErrors.h>

#include <arpa/inet.h>

#include <vector>

namespace android {

struct TSPacketizer::Track : public RefBase {
    Track(const sp<AMessage> &format,
          unsigned PID, unsigned streamType, unsigned streamID);

    unsigned PID() const;
    unsigned streamType() const;
    unsigned streamID() const;

    // Returns the previous value.
    unsigned incrementContinuityCounter();

    bool isAudio() const;
    bool isVideo() const;

    bool isH264() const;
    bool lacksADTSHeader() const;

    sp<ABuffer> prependCSD(const sp<ABuffer> &accessUnit) const;
    sp<ABuffer> prependADTSHeader(const sp<ABuffer> &accessUnit) const;

protected:
    virtual ~Track();

private:
    sp<AMessage> mFormat;

    unsigned mPID;
    unsigned mStreamType;
    unsigned mStreamID;
    unsigned mContinuityCounter;

    std::string mMIME;
    std::vector<sp<ABuffer>> mCSD;

    DISALLOW_EVIL_CONSTRUCTORS(Track);
};

TSPacketizer::Track::Track(
        const sp<AMessage> &format,
        unsigned PID, unsigned streamType, unsigned streamID)
    : mFormat(format),
      mPID(PID),
      mStreamType(streamType),
      mStreamID(streamID),
      mContinuityCounter(0) {
    CHECK(format->findString("mime", &mMIME));

    if (!strcasecmp(mMIME.c_str(), MEDIA_MIMETYPE_VIDEO_AVC)
            || !strcasecmp(mMIME.c_str(), MEDIA_MIMETYPE_AUDIO_AAC)) {
        for (size_t i = 0;; ++i) {
            sp<ABuffer> csd;
            if (!format->findBuffer(StringPrintf("csd-%d", i).c_str(), &csd)) {
                break;
            }

            mCSD.push_back(csd);
        }
    }
}

TSPacketizer::Track::~Track() {
}

unsigned TSPacketizer::Track::PID() const {
    return mPID;
}

unsigned TSPacketizer::Track::streamType() const {
    return mStreamType;
}

unsigned TSPacketizer::Track::streamID() const {
    return mStreamID;
}

unsigned TSPacketizer::Track::incrementContinuityCounter() {
    unsigned prevCounter = mContinuityCounter;

    if (++mContinuityCounter == 16) {
        mContinuityCounter = 0;
    }

    return prevCounter;
}

bool TSPacketizer::Track::isAudio() const {
    return !strncasecmp("audio/", mMIME.c_str(), 6);
}

bool TSPacketizer::Track::isVideo() const {
    return !strncasecmp("video/", mMIME.c_str(), 6);
}

bool TSPacketizer::Track::isH264() const {
    return !strcasecmp(mMIME.c_str(), MEDIA_MIMETYPE_VIDEO_AVC);
}

bool TSPacketizer::Track::lacksADTSHeader() const {
    if (strcasecmp(mMIME.c_str(), MEDIA_MIMETYPE_AUDIO_AAC)) {
        return false;
    }

    int32_t isADTS;
    if (mFormat->findInt32("is-adts", &isADTS) && isADTS != 0) {
        return false;
    }

    return true;
}

sp<ABuffer> TSPacketizer::Track::prependCSD(
        const sp<ABuffer> &accessUnit) const {
    size_t size = 0;
    for (const auto &csd : mCSD) {
        size += csd->size();
    }

    sp<ABuffer> dup = new ABuffer(accessUnit->size() + size);
    size_t offset = 0;
    for (const auto &csd : mCSD) {
        memcpy(dup->data() + offset, csd->data(), csd->size());
        offset += csd->size();
    }

    memcpy(dup->data() + offset, accessUnit->data(), accessUnit->size());

    return dup;
}

sp<ABuffer> TSPacketizer::Track::prependADTSHeader(
        const sp<ABuffer> &accessUnit) const {
    CHECK_EQ(mCSD.size(), 1u);

    const uint8_t *codec_specific_data = mCSD[0]->data();

    const uint32_t aac_frame_length = static_cast<uint32_t>(accessUnit->size() + 7);

    sp<ABuffer> dup = new ABuffer(aac_frame_length);

    unsigned profile = (codec_specific_data[0] >> 3) - 1;

    unsigned sampling_freq_index =
        ((codec_specific_data[0] & 7) << 1)
        | (codec_specific_data[1] >> 7);

    unsigned channel_configuration =
        (codec_specific_data[1] >> 3) & 0x0f;

    uint8_t *ptr = dup->data();

    *ptr++ = 0xff;
    *ptr++ = 0xf1;  // b11110001, ID=0, layer=0, protection_absent=1

    *ptr++ =
        profile << 6
        | sampling_freq_index << 2
        | ((channel_configuration >> 2) & 1);  // private_bit=0

    // original_copy=0, home=0, copyright_id_bit=0, copyright_id_start=0
    *ptr++ =
        (channel_configuration & 3) << 6
        | aac_frame_length >> 11;
    *ptr++ = (aac_frame_length >> 3) & 0xff;
    *ptr++ = (aac_frame_length & 7) << 5;

    // adts_buffer_fullness=0, number_of_raw_data_blocks_in_frame=0
    *ptr++ = 0;

    memcpy(ptr, accessUnit->data(), accessUnit->size());

    return dup;
}

////////////////////////////////////////////////////////////////////////////////

TSPacketizer::TSPacketizer()
    : mPATContinuityCounter(0),
      mPMTContinuityCounter(0) {
    initCrcTable();
}

TSPacketizer::~TSPacketizer() {
}

ssize_t TSPacketizer::addTrack(const sp<AMessage> &format) {
    std::string mime;
    CHECK(format->findString("mime", &mime));

    unsigned PIDStart;
    bool isVideo = !strncasecmp("video/", mime.c_str(), 6);
    bool isAudio = !strncasecmp("audio/", mime.c_str(), 6);

    if (isVideo) {
        PIDStart = 0x1011;
    } else if (isAudio) {
        PIDStart = 0x1100;
    } else {
        return ERROR_UNSUPPORTED;
    }

    unsigned streamType;
    unsigned streamIDStart;
    unsigned streamIDStop;

    if (!strcasecmp(mime.c_str(), MEDIA_MIMETYPE_VIDEO_AVC)) {
        streamType = 0x1b;
        streamIDStart = 0xe0;
        streamIDStop = 0xef;
    } else if (!strcasecmp(mime.c_str(), MEDIA_MIMETYPE_AUDIO_AAC)) {
        streamType = 0x0f;
        streamIDStart = 0xc0;
        streamIDStop = 0xdf;
    } else {
        return ERROR_UNSUPPORTED;
    }

    size_t numTracksOfThisType = 0;
    unsigned PID = PIDStart;

    for (const auto &track : mTracks) {
        if (track->streamType() == streamType) {
            ++numTracksOfThisType;
        }

        if ((isAudio && track->isAudio()) || (isVideo && track->isVideo())) {
            ++PID;
        }
    }

    unsigned streamID = static_cast<unsigned>(streamIDStart + numTracksOfThisType);
    if (streamID > streamIDStop) {
        return -ERANGE;
    }

    sp<Track> track = new Track(format, PID, streamType, streamID);
    size_t index = mTracks.size();
    mTracks.push_back(track);
    return index;
}

status_t TSPacketizer::packetize(
        size_t trackIndex,
        const sp<ABuffer> &_accessUnit,
        sp<ABuffer> *packets,
        uint32_t flags) {
    sp<ABuffer> accessUnit = _accessUnit;

    packets->clear();

    if (trackIndex >= mTracks.size()) {
        return -ERANGE;
    }

    int64_t timeUs;
    CHECK(accessUnit->meta()->findInt64("timeUs", &timeUs));

    const sp<Track> &track = mTracks[trackIndex];

    if (track->isH264()) {
        if (IsIDR(accessUnit)) {
            // prepend codec specific data, i.e. SPS and PPS.
            accessUnit = track->prependCSD(accessUnit);
        }
    } else if (track->lacksADTSHeader()) {
        accessUnit = track->prependADTSHeader(accessUnit);
    }

    // 0x47
    // transport_error_indicator = b0
    // payload_unit_start_indicator = b1
    // transport_priority = b0
    // PID
    // transport_scrambling_control = b00
    // adaptation_field_control = b??
    // continuity_counter = b????
    // -- payload follows
    // packet_startcode_prefix = 0x000001
    // stream_id
    // PES_packet_length = 0x????
    // reserved = b10
    // PES_scrambling_control = b00
    // PES_priority = b0
    // data_alignment_indicator = b1
    // copyright = b0
    // original_or_copy = b0
    // PTS_DTS_flags = b10  (PTS only)
    // ESCR_flag = b0
    // ES_rate_flag = b0
    // DSM_trick_mode_flag = b0
    // additional_copy_info_flag = b0
    // PES_CRC_flag = b0
    // PES_extension_flag = b0
    // PES_header_data_length = 0x05
    // reserved = b0010 (PTS)
    // PTS[32..30] = b???
    // reserved = b1
    // PTS[29..15] = b??? ???? ???? ???? (15 bits)
    // reserved = b1
    // PTS[14..0] = b??? ???? ???? ???? (15 bits)
    // reserved = b1
    // the first fragment of "buffer" follows

    size_t numTSPackets;
    if (accessUnit->size() <= 170) {
        numTSPackets = 1;
    } else {
        numTSPackets = 1 + ((accessUnit->size() - 170) + 183) / 184;
    }

    if (flags & EMIT_PAT_AND_PMT) {
        numTSPackets += 2;
    }

    if (flags & EMIT_PCR) {
        ++numTSPackets;
    }

    sp<ABuffer> buffer = new ABuffer(numTSPackets * 188);
    uint8_t *packetDataStart = buffer->data();

    if (flags & EMIT_PAT_AND_PMT) {
        // Program Association Table (PAT):
        // 0x47
        // transport_error_indicator = b0
        // payload_unit_start_indicator = b1
        // transport_priority = b0
        // PID = b0000000000000 (13 bits)
        // transport_scrambling_control = b00
        // adaptation_field_control = b01 (no adaptation field, payload only)
        // continuity_counter = b????
        // skip = 0x00
        // --- payload follows
        // table_id = 0x00
        // section_syntax_indicator = b1
        // must_be_zero = b0
        // reserved = b11
        // section_length = 0x00d
        // transport_stream_id = 0x0000
        // reserved = b11
        // version_number = b00001
        // current_next_indicator = b1
        // section_number = 0x00
        // last_section_number = 0x00
        //   one program follows:
        //   program_number = 0x0001
        //   reserved = b111
        //   program_map_PID = kPID_PMT (13 bits!)
        // CRC = 0x????????

        if (++mPATContinuityCounter == 16) {
            mPATContinuityCounter = 0;
        }

        uint8_t *ptr = packetDataStart;
        *ptr++ = 0x47;
        *ptr++ = 0x40;
        *ptr++ = 0x00;
        *ptr++ = 0x10 | mPATContinuityCounter;
        *ptr++ = 0x00;

        const uint8_t *crcDataStart = ptr;
        *ptr++ = 0x00;
        *ptr++ = 0xb0;
        *ptr++ = 0x0d;
        *ptr++ = 0x00;
        *ptr++ = 0x00;
        *ptr++ = 0xc3;
        *ptr++ = 0x00;
        *ptr++ = 0x00;
        *ptr++ = 0x00;
        *ptr++ = 0x01;
        *ptr++ = 0xe0 | (kPID_PMT >> 8);
        *ptr++ = kPID_PMT & 0xff;

        CHECK_EQ(ptr - crcDataStart, 12);
        uint32_t crc = htonl(crc32(crcDataStart, ptr - crcDataStart));
        memcpy(ptr, &crc, 4);
        ptr += 4;

        size_t sizeLeft = packetDataStart + 188 - ptr;
        memset(ptr, 0xff, sizeLeft);

        packetDataStart += 188;

        // Program Map (PMT):
        // 0x47
        // transport_error_indicator = b0
        // payload_unit_start_indicator = b1
        // transport_priority = b0
        // PID = kPID_PMT (13 bits)
        // transport_scrambling_control = b00
        // adaptation_field_control = b01 (no adaptation field, payload only)
        // continuity_counter = b????
        // skip = 0x00
        // -- payload follows
        // table_id = 0x02
        // section_syntax_indicator = b1
        // must_be_zero = b0
        // reserved = b11
        // section_length = 0x???
        // program_number = 0x0001
        // reserved = b11
        // version_number = b00001
        // current_next_indicator = b1
        // section_number = 0x00
        // last_section_number = 0x00
        // reserved = b111
        // PCR_PID = kPCR_PID (13 bits)
        // reserved = b1111
        // program_info_length = 0x000
        //   one or more elementary stream descriptions follow:
        //   stream_type = 0x??
        //   reserved = b111
        //   elementary_PID = b? ???? ???? ???? (13 bits)
        //   reserved = b1111
        //   ES_info_length = 0x000
        // CRC = 0x????????

        if (++mPMTContinuityCounter == 16) {
            mPMTContinuityCounter = 0;
        }

        size_t section_length = 5 * mTracks.size() + 4 + 9;

        ptr = packetDataStart;
        *ptr++ = 0x47;
        *ptr++ = 0x40 | (kPID_PMT >> 8);
        *ptr++ = kPID_PMT & 0xff;
        *ptr++ = 0x10 | mPMTContinuityCounter;
        *ptr++ = 0x00;

        crcDataStart = ptr;
        *ptr++ = 0x02;
        *ptr++ = 0xb0 | (section_length >> 8);
        *ptr++ = section_length & 0xff;
        *ptr++ = 0x00;
        *ptr++ = 0x01;
        *ptr++ = 0xc3;
        *ptr++ = 0x00;
        *ptr++ = 0x00;
        *ptr++ = 0xe0 | (kPID_PCR >> 8);
        *ptr++ = kPID_PCR & 0xff;
        *ptr++ = 0xf0;
        *ptr++ = 0x00;

        for (size_t i = 0; i < mTracks.size(); ++i) {
            const sp<Track> &track = mTracks[i];

            *ptr++ = track->streamType();
            *ptr++ = 0xe0 | (track->PID() >> 8);
            *ptr++ = track->PID() & 0xff;
            *ptr++ = 0xf0;
            *ptr++ = 0x00;
        }

        CHECK_EQ(static_cast<size_t>(ptr - crcDataStart),
                 12 + mTracks.size() * 5);

        crc = htonl(crc32(crcDataStart, ptr - crcDataStart));
        memcpy(ptr, &crc, 4);
        ptr += 4;

        sizeLeft = packetDataStart + 188 - ptr;
        memset(ptr, 0xff, sizeLeft);

        packetDataStart += 188;
    }

    if (flags & EMIT_PCR) {
        // PCR stream
        // 0x47
        // transport_error_indicator = b0
        // payload_unit_start_indicator = b1
        // transport_priority = b0
        // PID = kPCR_PID (13 bits)
        // transport_scrambling_control = b00
        // adaptation_field_control = b10 (adaptation field only, no payload)
        // continuity_counter = b0000 (does not increment)
        // adaptation_field_length = 183
        // discontinuity_indicator = b0
        // random_access_indicator = b0
        // elementary_stream_priority_indicator = b0
        // PCR_flag = b1
        // OPCR_flag = b0
        // splicing_point_flag = b0
        // transport_private_data_flag = b0
        // adaptation_field_extension_flag = b0
        // program_clock_reference_base = b?????????????????????????????????
        // reserved = b111111
        // program_clock_reference_extension = b?????????

        int64_t nowUs = timeUs;

        uint64_t PCR = nowUs * 27;  // PCR based on a 27MHz clock
        uint64_t PCR_base = PCR / 300;
        uint32_t PCR_ext = PCR % 300;

        uint8_t *ptr = packetDataStart;
        *ptr++ = 0x47;
        *ptr++ = 0x40 | (kPID_PCR >> 8);
        *ptr++ = kPID_PCR & 0xff;
        *ptr++ = 0x20;
        *ptr++ = 0xb7;  // adaptation_field_length
        *ptr++ = 0x10;
        *ptr++ = (PCR_base >> 25) & 0xff;
        *ptr++ = (PCR_base >> 17) & 0xff;
        *ptr++ = (PCR_base >> 9) & 0xff;
        *ptr++ = ((PCR_base & 1) << 7) | 0x7e | ((PCR_ext >> 8) & 1);
        *ptr++ = (PCR_ext & 0xff);

        size_t sizeLeft = packetDataStart + 188 - ptr;
        memset(ptr, 0xff, sizeLeft);

        packetDataStart += 188;
    }

    uint32_t PTS = static_cast<uint32_t>((timeUs * 9ll) / 100ll);

    size_t PES_packet_length = accessUnit->size() + 8;
    bool padding = (accessUnit->size() < (188 - 18));

    if (PES_packet_length >= 65536) {
        // This really should only happen for video.
        CHECK(track->isVideo());

        // It's valid to set this to 0 for video according to the specs.
        PES_packet_length = 0;
    }

    uint8_t *ptr = packetDataStart;
    *ptr++ = 0x47;
    *ptr++ = 0x40 | (track->PID() >> 8);
    *ptr++ = track->PID() & 0xff;
    *ptr++ = (padding ? 0x30 : 0x10) | track->incrementContinuityCounter();

    if (padding) {
        size_t paddingSize = 188 - 18 - accessUnit->size();
        *ptr++ = paddingSize - 1;
        if (paddingSize >= 2) {
            *ptr++ = 0x00;
            memset(ptr, 0xff, paddingSize - 2);
            ptr += paddingSize - 2;
        }
    }

    *ptr++ = 0x00;
    *ptr++ = 0x00;
    *ptr++ = 0x01;
    *ptr++ = track->streamID();
    *ptr++ = PES_packet_length >> 8;
    *ptr++ = PES_packet_length & 0xff;
    *ptr++ = 0x84;
    *ptr++ = 0x80;
    *ptr++ = 0x05;
    *ptr++ = 0x20 | (((PTS >> 30) & 7) << 1) | 1;
    *ptr++ = (PTS >> 22) & 0xff;
    *ptr++ = (((PTS >> 15) & 0x7f) << 1) | 1;
    *ptr++ = (PTS >> 7) & 0xff;
    *ptr++ = ((PTS & 0x7f) << 1) | 1;

    // 18 bytes of TS/PES header leave 188 - 18 = 170 bytes for the payload

    size_t sizeLeft = packetDataStart + 188 - ptr;
    size_t copy = accessUnit->size();
    if (copy > sizeLeft) {
        copy = sizeLeft;
    }

    memcpy(ptr, accessUnit->data(), copy);
    ptr += copy;
    CHECK_EQ(sizeLeft, copy);
    memset(ptr, 0xff, sizeLeft - copy);

    packetDataStart += 188;

    size_t offset = copy;
    while (offset < accessUnit->size()) {
        bool padding = (accessUnit->size() - offset) < (188 - 4);

        // for subsequent fragments of "buffer":
        // 0x47
        // transport_error_indicator = b0
        // payload_unit_start_indicator = b0
        // transport_priority = b0
        // PID = b0 0001 1110 ???? (13 bits) [0x1e0 + 1 + sourceIndex]
        // transport_scrambling_control = b00
        // adaptation_field_control = b??
        // continuity_counter = b????
        // the fragment of "buffer" follows.

        uint8_t *ptr = packetDataStart;
        *ptr++ = 0x47;
        *ptr++ = 0x00 | (track->PID() >> 8);
        *ptr++ = track->PID() & 0xff;

        *ptr++ = (padding ? 0x30 : 0x10) | track->incrementContinuityCounter();

        if (padding) {
            size_t paddingSize = 188 - 4 - (accessUnit->size() - offset);
            *ptr++ = paddingSize - 1;
            if (paddingSize >= 2) {
                *ptr++ = 0x00;
                memset(ptr, 0xff, paddingSize - 2);
                ptr += paddingSize - 2;
            }
        }

        // 4 bytes of TS header leave 188 - 4 = 184 bytes for the payload

        size_t sizeLeft = packetDataStart + 188 - ptr;
        size_t copy = accessUnit->size() - offset;
        if (copy > sizeLeft) {
            copy = sizeLeft;
        }

        memcpy(ptr, accessUnit->data() + offset, copy);
        ptr += copy;
        CHECK_EQ(sizeLeft, copy);
        memset(ptr, 0xff, sizeLeft - copy);

        offset += copy;
        packetDataStart += 188;
    }

    CHECK(packetDataStart == buffer->data() + buffer->capacity());

    *packets = buffer;

    return OK;
}

void TSPacketizer::initCrcTable() {
    uint32_t poly = 0x04C11DB7;

    for (int i = 0; i < 256; i++) {
        uint32_t crc = i << 24;
        for (int j = 0; j < 8; j++) {
            crc = (crc << 1) ^ ((crc & 0x80000000) ? (poly) : 0);
        }
        mCrcTable[i] = crc;
    }
}

uint32_t TSPacketizer::crc32(const uint8_t *start, size_t size) const {
    uint32_t crc = 0xFFFFFFFF;
    const uint8_t *p;

    for (p = start; p < start + size; ++p) {
        crc = (crc << 8) ^ mCrcTable[((crc >> 24) ^ *p) & 0xFF];
    }

    return crc;
}

}  // namespace android

