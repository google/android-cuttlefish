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

#ifndef A_LOOPER_H_

#define A_LOOPER_H_

#include "ABase.h"

#include <utils/Errors.h>
#include <utils/threads.h>

#include <deque>
#include <memory>

namespace android {

struct AHandler;
struct AMessage;

struct ALooper {
    typedef int32_t event_id;
    typedef int32_t handler_id;

    ALooper();

    static handler_id registerHandler(std::shared_ptr<ALooper> looper, 
                                      const std::shared_ptr<AHandler> &handler);
    static void unregisterHandler(handler_id handlerID);

    status_t start(bool runOnCallingThread = false);
    status_t stop();

    void setName(const char * /* name */) {}

    static int64_t GetNowUs();

    virtual ~ALooper();

private:
    friend struct ALooperRoster;

    struct Event {
        int64_t mWhenUs;
        std::shared_ptr<AMessage> mMessage;
    };

    Mutex mLock;
    Condition mQueueChangedCondition;

    std::deque<Event> mEventQueue;

    struct LooperThread;
    std::shared_ptr<LooperThread> mThread;
    bool mRunningLocally;

    void post(const std::shared_ptr<AMessage> &msg, int64_t delayUs);
    bool loop();

    DISALLOW_EVIL_CONSTRUCTORS(ALooper);
};

}  // namespace android

#endif  // A_LOOPER_H_
