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

#include <https/BaseConnection.h>

#include <https/SafeCallbackable.h>
#include <https/PlainSocket.h>

BaseConnection::BaseConnection(std::shared_ptr<RunLoop> runLoop, int sock)
    : mRunLoop(runLoop),
      mSocket(std::make_unique<PlainSocket>(mRunLoop, sock)),
      mInBufferLen(0),
      mSendPending(false) {
}

void BaseConnection::run() {
    receiveClientRequest();
}

void BaseConnection::receiveClientRequest() {
    mSocket->postRecv(makeSafeCallback(this, &BaseConnection::onClientRequest));
}

void BaseConnection::onClientRequest() {
    static constexpr size_t kMaxChunkSize = 8192;

    mInBuffer.resize(mInBufferLen + kMaxChunkSize);

    ssize_t n;
    do {
        n = mSocket->recv(&mInBuffer[mInBufferLen], kMaxChunkSize);
    } while (n < 0 && errno == EINTR);

    if (n <= 0) {
        onDisconnect((n < 0) ? -errno : 0);
        return;
    }

    mInBufferLen += static_cast<size_t>(n);

    while (mInBufferLen > 0) {
        n = processClientRequest(mInBuffer.data(), mInBufferLen);

        if (n <= 0) {
            break;
        }

        mInBuffer.erase(mInBuffer.begin(), mInBuffer.begin() + n);
        mInBufferLen -= n;
    }

    if (n <= 0 && n != -EAGAIN && n != EWOULDBLOCK) {
        onDisconnect(n);
        return;
    }

    receiveClientRequest();
}

void BaseConnection::send(const void *_data, size_t size) {
    const uint8_t *data = static_cast<const uint8_t *>(_data);
    std::copy(data, data + size, std::back_inserter(mOutBuffer));

    if (!mSendPending) {
        mSendPending = true;
        mSocket->postSend(
                makeSafeCallback(this, &BaseConnection::sendOutputData));
    }
}

void BaseConnection::sendOutputData() {
    mSendPending = false;

    const size_t size = mOutBuffer.size();
    size_t offset = 0;

    while (offset < size) {
        ssize_t n = mSocket->send(mOutBuffer.data() + offset, size - offset);

        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }

            assert(!"Should not be here");
        } else if (n == 0) {
            // The remote seems gone, clear the output buffer and disconnect.
            offset = size;
            break;
        }

        offset += static_cast<size_t>(n);
    }

    mOutBuffer.erase(mOutBuffer.begin(), mOutBuffer.begin() + offset);

    if (!mOutBuffer.empty()) {
        mSendPending = true;

        mSocket->postSend(
                makeSafeCallback(this, &BaseConnection::sendOutputData));
        return;
    }
}

int BaseConnection::fd() const {
    return mSocket->fd();
}

