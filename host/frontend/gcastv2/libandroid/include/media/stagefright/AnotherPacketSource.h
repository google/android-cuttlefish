/*
 * Copyright (C) 2010 The Android Open Source Project
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

#ifndef ANOTHER_PACKET_SOURCE_H_

#define ANOTHER_PACKET_SOURCE_H_

#include <media/stagefright/foundation/ABase.h>
#include <media/stagefright/ATSParser.h>
#include <media/stagefright/MediaSource.h>
#include <utils/threads.h>

#include <list>

namespace android {

struct ABuffer;

struct AnotherPacketSource : public MediaSource {
    AnotherPacketSource(const std::shared_ptr<MetaData> &meta);

    void setFormat(const std::shared_ptr<MetaData> &meta);

    virtual status_t start(MetaData *params = NULL);
    virtual status_t stop();
    virtual std::shared_ptr<MetaData> getFormat();

    virtual status_t read(
            MediaBuffer **buffer, const ReadOptions *options = NULL);

    bool hasBufferAvailable(status_t *finalResult);

    // Returns the difference between the last and the first queued
    // presentation timestamps since the last discontinuity (if any).
    int64_t getBufferedDurationUs(status_t *finalResult);

    status_t nextBufferTime(int64_t *timeUs);

    void queueAccessUnit(const std::shared_ptr<ABuffer> &buffer);

    void queueDiscontinuity(
            ATSParser::DiscontinuityType type, const std::shared_ptr<AMessage> &extra);

    void signalEOS(status_t result);

    status_t dequeueAccessUnit(std::shared_ptr<ABuffer> *buffer);

    bool isFinished(int64_t duration) const;

    virtual ~AnotherPacketSource();

private:
    Mutex mLock;
    Condition mCondition;

    bool mIsAudio;
    std::shared_ptr<MetaData> mFormat;
    int64_t mLastQueuedTimeUs;
    std::list<std::shared_ptr<ABuffer>> mBuffers;
    status_t mEOSResult;

    bool wasFormatChange(int32_t discontinuityType) const;

    DISALLOW_EVIL_CONSTRUCTORS(AnotherPacketSource);
};


}  // namespace android

#endif  // ANOTHER_PACKET_SOURCE_H_
