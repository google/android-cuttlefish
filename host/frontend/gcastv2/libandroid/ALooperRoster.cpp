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
#define LOG_TAG "ALooperRoster"
#include <utils/Log.h>

#include <media/stagefright/foundation/ALooperRoster.h>

#include <media/stagefright/foundation/ADebug.h>
#include <media/stagefright/foundation/AHandler.h>
#include <media/stagefright/foundation/AMessage.h>

namespace android {

ALooperRoster::ALooperRoster()
    : mNextHandlerID(1) {
}

ALooper::handler_id ALooperRoster::registerHandler(
        const std::shared_ptr<ALooper> looper, const std::shared_ptr<AHandler> &handler) {
    Mutex::Autolock autoLock(mLock);

    if (handler->id() != 0) {
        CHECK(!"A handler must only be registered once.");
        return INVALID_OPERATION;
    }

    handler->mLooper = looper.get();

    HandlerInfo info;
    info.mLooper = looper;
    info.mHandler = handler;
    ALooper::handler_id handlerID = mNextHandlerID++;
    mHandlers[handlerID] = info;

    handler->setID(handlerID);

    return handlerID;
}

void ALooperRoster::unregisterHandler(ALooper::handler_id handlerID) {
    Mutex::Autolock autoLock(mLock);

    auto it = mHandlers.find(handlerID);
    CHECK(it != mHandlers.end());

    const HandlerInfo &info = it->second;
    std::shared_ptr<AHandler> handler = info.mHandler.lock();

    if (handler != NULL) {
        handler->setID(0);
    }

    mHandlers.erase(it);
}

void ALooperRoster::postMessage(
        const std::shared_ptr<AMessage> &msg, int64_t delayUs) {
    Mutex::Autolock autoLock(mLock);

    auto it = mHandlers.find(msg->target());

    if (it == mHandlers.end()) {
        LOG(WARNING) << "failed to post message. Target handler not registered.";
        return;
    }

    const HandlerInfo &info = it->second;
    info.mLooper->post(msg, delayUs);
}

void ALooperRoster::deliverMessage(const std::shared_ptr<AMessage> &msg) {
    std::shared_ptr<AHandler> handler;

    {
        Mutex::Autolock autoLock(mLock);

        auto it = mHandlers.find(msg->target());

        if (it == mHandlers.end()) {
            LOG(WARNING) << "failed to deliver message. Target handler not registered.";
            return;
        }

        const HandlerInfo &info = it->second;
        handler = info.mHandler.lock();

        if (!handler) {
            mHandlers.erase(it);
            return;
        }
    }

    handler->onMessageReceived(msg);
}

}  // namespace android
