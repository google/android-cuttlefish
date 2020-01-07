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

#ifndef TS_PACKETIZER_H_

#define TS_PACKETIZER_H_

#include <media/stagefright/foundation/ABase.h>
#include <utils/Errors.h>
#include <utils/RefBase.h>

#include <vector>

namespace android {

struct ABuffer;
struct AMessage;

struct TSPacketizer : public RefBase {
    TSPacketizer();

    // Returns trackIndex or error.
    ssize_t addTrack(const sp<AMessage> &format);

    enum {
        EMIT_PAT_AND_PMT = 1,
        EMIT_PCR         = 2,
    };
    status_t packetize(
            size_t trackIndex, const sp<ABuffer> &accessUnit,
            sp<ABuffer> *packets,
            uint32_t flags);

protected:
    virtual ~TSPacketizer();

private:
    enum {
        kPID_PMT = 0x100,
        kPID_PCR = 0x1000,
    };

    struct Track;

    std::vector<sp<Track>> mTracks;

    unsigned mPATContinuityCounter;
    unsigned mPMTContinuityCounter;

    uint32_t mCrcTable[256];

    void initCrcTable();
    uint32_t crc32(const uint8_t *start, size_t size) const;

    DISALLOW_EVIL_CONSTRUCTORS(TSPacketizer);
};

}  // namespace android

#endif  // TS_PACKETIZER_H_

