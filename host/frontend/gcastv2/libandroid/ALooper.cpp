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

//#define LOG_NDEBUG 0
#define LOG_TAG "ALooper"
#include <utils/Log.h>

#include <time.h>

#include <media/stagefright/foundation/ALooper.h>

#include <media/stagefright/foundation/AHandler.h>
#include <media/stagefright/foundation/ALooperRoster.h>
#include <media/stagefright/foundation/AMessage.h>

namespace android {

ALooperRoster gLooperRoster;

struct ALooper::LooperThread : public Thread {
    LooperThread(ALooper *looper)
        : mLooper(looper) {
    }

    virtual bool threadLoop() {
        return mLooper->loop();
    }

    virtual ~LooperThread() {}

private:
    ALooper *mLooper;

    DISALLOW_EVIL_CONSTRUCTORS(LooperThread);
};

// static
int64_t ALooper::GetNowUs() {
    struct timespec ts;
    int res = clock_gettime(CLOCK_MONOTONIC, &ts);
    CHECK(!res);

    return static_cast<int64_t>(ts.tv_sec) * 1000000
        + (ts.tv_nsec + 500) / 1000;
}

ALooper::ALooper()
    : mRunningLocally(false) {
}

ALooper::~ALooper() {
    stop();
}

ALooper::handler_id ALooper::registerHandler(std::shared_ptr<ALooper> looper, 
                                             const std::shared_ptr<AHandler> &handler) {
    return gLooperRoster.registerHandler(looper, handler);
}
void ALooper::unregisterHandler(handler_id handlerID) {
    gLooperRoster.unregisterHandler(handlerID);
}

status_t ALooper::start(bool runOnCallingThread) {
    if (runOnCallingThread) {
        {
            Mutex::Autolock autoLock(mLock);

            if (mThread != NULL || mRunningLocally) {
                return INVALID_OPERATION;
            }

            mRunningLocally = true;
        }

        do {
        } while (loop());

        return OK;
    }

    Mutex::Autolock autoLock(mLock);

    if (mThread != NULL || mRunningLocally) {
        return INVALID_OPERATION;
    }

    mThread.reset(new LooperThread(this));

    status_t err = mThread->run("ALooper");
    if (err != OK) {
        mThread.reset();
    }

    return err;
}

status_t ALooper::stop() {
    std::shared_ptr<LooperThread> thread;
    bool runningLocally;

    {
        Mutex::Autolock autoLock(mLock);

        thread = mThread;
        runningLocally = mRunningLocally;
        mThread.reset();
        mRunningLocally = false;
    }

    if (thread == NULL && !runningLocally) {
        return INVALID_OPERATION;
    }

    mQueueChangedCondition.signal();

    if (thread != NULL) {
        thread->requestExit();
    }

    return OK;
}

void ALooper::post(const std::shared_ptr<AMessage> &msg, int64_t delayUs) {
    Mutex::Autolock autoLock(mLock);

    int64_t whenUs;
    if (delayUs > 0) {
        whenUs = GetNowUs() + delayUs;
    } else {
        whenUs = GetNowUs();
    }

    auto it = mEventQueue.begin();
    while (it != mEventQueue.end() && (*it).mWhenUs <= whenUs) {
        ++it;
    }

    Event event;
    event.mWhenUs = whenUs;
    event.mMessage = msg;

    if (it == mEventQueue.begin()) {
        mQueueChangedCondition.signal();
    }

    mEventQueue.insert(it, event);
}

bool ALooper::loop() {
    Event event;

    {
        Mutex::Autolock autoLock(mLock);
        if (mThread == NULL && !mRunningLocally) {
            return false;
        }
        if (mEventQueue.empty()) {
            mQueueChangedCondition.wait(mLock);
            return true;
        }
        int64_t whenUs = (*mEventQueue.begin()).mWhenUs;
        int64_t nowUs = GetNowUs();

        if (whenUs > nowUs) {
            int64_t delayUs = whenUs - nowUs;
            mQueueChangedCondition.waitRelative(mLock, delayUs * 1000ll);

            return true;
        }

        event = mEventQueue.front();
        mEventQueue.pop_front();
    }

    gLooperRoster.deliverMessage(event.mMessage);

    // NOTE: It's important to note that at this point our "ALooper" object
    // may no longer exist (its final reference may have gone away while
    // delivering the message). We have made sure, however, that loop()
    // won't be called again.

    return true;
}

}  // namespace android
