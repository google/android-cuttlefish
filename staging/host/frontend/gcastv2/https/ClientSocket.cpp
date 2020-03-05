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

#include <https/ClientSocket.h>

#include <https/HTTPServer.h>
#include <https/RunLoop.h>
#include <https/SafeCallbackable.h>
#include <https/ServerSocket.h>

#include <android-base/logging.h>

#include <cstdlib>

ClientSocket::ClientSocket(
        std::shared_ptr<RunLoop> rl,
        HTTPServer *server,
        ServerSocket *parent,
        const sockaddr_in &addr,
        int sock)
    : mRunLoop(rl),
      mServer(server),
      mParent(parent),
      mRemoteAddr(addr),
      mInBufferLen(0),
      mSendPending(false),
      mDisconnecting(false) {
    if (parent->transportType() == ServerSocket::TransportType::TLS) {
        mImplSSL = std::make_shared<SSLSocket>(
                mRunLoop,
                sock,
                *server->certificate_pem_path(),
                *server->private_key_pem_path());
    } else {
        mImplPlain = std::make_shared<PlainSocket>(mRunLoop, sock);
    }
}

void ClientSocket::run() {
    getImpl()->postRecv(makeSafeCallback(this, &ClientSocket::handleIncomingData));
}

int ClientSocket::fd() const {
    return getImpl()->fd();
}

void ClientSocket::setWebSocketHandler(
        std::shared_ptr<WebSocketHandler> handler) {
    mWebSocketHandler = handler;
    mWebSocketHandler->setClientSocket(shared_from_this());
}

void ClientSocket::handleIncomingData() {
    mInBuffer.resize(mInBufferLen + 1024);

    ssize_t n;
    do {
        n = getImpl()->recv(mInBuffer.data() + mInBufferLen, 1024);
    } while (n < 0 && errno == EINTR);

    if (n == 0) {
        if (errno == 0) {
            // Don't process any data if there was an actual failure.
            // This could be an authentication failure for example...
            // We shouldn't trust anything the client says.
            (void)handleRequest(true /* sawEOS */);
        }

        disconnect();
        return;
    } else if (n < 0) {
        LOG(ERROR)
            << "recv returned error "
            << errno
            << " ("
            << strerror(errno)
            << ")";

        mParent->onClientSocketClosed(fd());
        return;
    }

    mInBufferLen += static_cast<size_t>(n);
    const bool closeConnection = handleRequest(false /* sawEOS */);

    if (closeConnection) {
        disconnect();
    } else {
        getImpl()->postRecv(
                makeSafeCallback(this, &ClientSocket::handleIncomingData));
    }
}

void ClientSocket::disconnect() {
    if (mDisconnecting) {
        return;
    }

    mDisconnecting = true;

    finishDisconnect();
}

void ClientSocket::finishDisconnect() {
    if (!mSendPending) {
        // Our output queue may now be empty, but the underlying socket
        // implementation may still buffer something that we need to flush
        // first.
        getImpl()->postFlush(
                makeSafeCallback<ClientSocket>(this, [](ClientSocket *me) {
                    me->mParent->onClientSocketClosed(me->fd());
                }));
    }
}

bool ClientSocket::handleRequest(bool isEOS) {
    if (mWebSocketHandler) {
        ssize_t n = mWebSocketHandler->handleRequest(
                mInBuffer.data(), mInBufferLen, isEOS);

        LOG(VERBOSE)
            << "handleRequest returned "
            << n
            << " when called with "
            << mInBufferLen
            << ", eos="
            << isEOS;

        if (n > 0) {
            mInBuffer.erase(mInBuffer.begin(), mInBuffer.begin() + n);
            mInBufferLen -= n;
        }

        // NOTE: Do not return true, i.e. disconnect, if the json handler
        // returns 0 bytes read, it simply means we need more data to continue.
        return n < 0;
    }

    size_t len = mInBufferLen;

    if (!isEOS) {
        static const char kPattern[] = "\r\n\r\n";

        // Don't count the trailing NUL.
        static constexpr size_t kPatternLength = sizeof(kPattern) - 1;

        size_t i = 0;
        while (i + kPatternLength <= mInBufferLen
                && memcmp(mInBuffer.data() + i, kPattern, kPatternLength)) {
            ++i;
        }

        if (i + kPatternLength > mInBufferLen) {
            return false;
        }

        // Found a match.
        len = i + kPatternLength;
    }

    const bool closeConnection =
        mServer->handleSingleRequest(this, mInBuffer.data(), len, isEOS);

    mInBuffer.clear();
    mInBufferLen = 0;

    return closeConnection;
}

void ClientSocket::queueOutputData(const uint8_t *data, size_t size) {
    std::copy(data, data + size, std::back_inserter(mOutBuffer));

    if (!mSendPending) {
        mSendPending = true;
        getImpl()->postSend(makeSafeCallback(this, &ClientSocket::sendOutputData));
    }
}

sockaddr_in ClientSocket::remoteAddr() const {
    return mRemoteAddr;
}

void ClientSocket::queueResponse(
        const std::string &response, const std::string &body) {
    std::copy(response.begin(), response.end(), std::back_inserter(mOutBuffer));
    std::copy(body.begin(), body.end(), std::back_inserter(mOutBuffer));

    if (!mSendPending) {
        mSendPending = true;
        getImpl()->postSend(makeSafeCallback(this, &ClientSocket::sendOutputData));
    }
}

void ClientSocket::sendOutputData() {
    mSendPending = false;

    const size_t size = mOutBuffer.size();
    size_t offset = 0;

    while (offset < size) {
        ssize_t n = getImpl()->send(mOutBuffer.data() + offset, size - offset);

        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }

            assert(!"Should not be here");
        } else if (n == 0) {
            // The remote seems gone, clear the output buffer and disconnect.
            offset = size;
            mDisconnecting = true;
            break;
        }

        offset += static_cast<size_t>(n);
    }

    mOutBuffer.erase(mOutBuffer.begin(), mOutBuffer.begin() + offset);

    if (!mOutBuffer.empty()) {
        mSendPending = true;
        getImpl()->postSend(makeSafeCallback(this, &ClientSocket::sendOutputData));
        return;
    }

    if (mDisconnecting) {
        finishDisconnect();
    }
}

