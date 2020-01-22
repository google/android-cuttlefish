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

#pragma once

#include <https/RunLoop.h>

#include <sys/socket.h>
#include "common/libs/fs/vm_sockets.h"

#include <memory>

struct HostToGuestComms : std::enable_shared_from_this<HostToGuestComms> {
    using ReceiveCb = std::function<void(const void *data, size_t size)>;

    explicit HostToGuestComms(
            std::shared_ptr<RunLoop> runLoop,
            bool isServer,
            uint32_t cid,
            uint16_t port,
            ReceiveCb onReceive);

    explicit HostToGuestComms(
            std::shared_ptr<RunLoop> runLoop,
            bool isServer,
            int fd,
            ReceiveCb onReceive);

    ~HostToGuestComms();

    void start();

    void send(const void *data, size_t size, bool addFraming = true);

private:
    std::shared_ptr<RunLoop> mRunLoop;
    bool mIsServer;
    ReceiveCb mOnReceive;

    int mServerSock;
    int mSock;
    sockaddr_vm mConnectToAddr;

    std::vector<uint8_t> mInBuffer;
    size_t mInBufferLen;

    std::mutex mLock;
    std::vector<uint8_t> mOutBuffer;
    bool mSendPending;

    bool mConnected;

    void onServerConnection();
    void onSocketReceive();
    void onSocketSend();
    void drainInBuffer();

    void onAttemptToConnect(const sockaddr_vm &addr);
    void onCheckConnection(const sockaddr_vm &addr);
    void onConnected();
};

