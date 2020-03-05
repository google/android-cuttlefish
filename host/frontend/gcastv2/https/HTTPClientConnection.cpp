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

#include <https/HTTPClientConnection.h>

#include <https/HTTPRequestResponse.h>
#include <https/PlainSocket.h>
#include <https/RunLoop.h>
#include <https/SafeCallbackable.h>
#include <https/SSLSocket.h>

#include <https/Support.h>

#include <glog/logging.h>

#include <arpa/inet.h>
#include <cerrno>
#include <netinet/in.h>
#include <unistd.h>

using namespace android;

HTTPClientConnection::HTTPClientConnection(
        std::shared_ptr<RunLoop> rl,
        std::shared_ptr<WebSocketHandler> webSocketHandler,
        std::string_view path,
        ServerSocket::TransportType transportType,
        const std::optional<std::string> &trusted_pem_path)
    : mInitCheck(-ENODEV),
      mRunLoop(rl),
      mWebSocketHandler(webSocketHandler),
      mPath(path),
      mTransportType(transportType),
      mSendPending(false),
      mInBufferLen(0),
      mWebSocketMode(false) {
    int sock;

    sock = socket(PF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        mInitCheck = -errno;
        goto bail;
    }

    makeFdNonblocking(sock);

    if (mTransportType == ServerSocket::TransportType::TLS) {
        CHECK(trusted_pem_path.has_value());

        mImpl = std::make_shared<SSLSocket>(
                mRunLoop, sock, 0 /* flags */, *trusted_pem_path);
    } else {
        mImpl = std::make_shared<PlainSocket>(mRunLoop, sock);
    }

    mInitCheck = 0;
    return;

bail:
    ;
}

int HTTPClientConnection::initCheck() const {
    return mInitCheck;
}

int HTTPClientConnection::connect(const char *host, uint16_t port) {
    if (mInitCheck < 0) {
        return mInitCheck;
    }

    sockaddr_in addr;
    memset(addr.sin_zero, 0, sizeof(addr.sin_zero));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = inet_addr(host);

    mRemoteAddr = addr;

    int res = ::connect(
            mImpl->fd(), reinterpret_cast<sockaddr *>(&addr), sizeof(addr));

    if (res < 0 && errno != EINPROGRESS) {
        return -errno;
    }

    mImpl->postSend(makeSafeCallback(this, &HTTPClientConnection::sendRequest));

    return 0;
}

void HTTPClientConnection::sendRequest() {
    std::string request;
    request =
        "GET " + mPath + " HTTP/1.1\r\n"
        "Connection: Upgrade\r\n"
        "Upgrade: websocket\r\n"
        "Sec-WebSocket-Version: 13\r\n"
        "Sec-WebSocket-Key: foobar\r\n"
        "\r\n";

    CHECK(mRunLoop->isCurrentThread());
    std::copy(request.begin(), request.end(), std::back_inserter(mOutBuffer));

    if (!mSendPending) {
        mSendPending = true;
        mImpl->postSend(
                makeSafeCallback(this, &HTTPClientConnection::sendOutputData));
    }

    mImpl->postRecv(
            makeSafeCallback(this, &HTTPClientConnection::receiveResponse));
}

void HTTPClientConnection::receiveResponse() {
    mInBuffer.resize(mInBufferLen + 1024);

    ssize_t n;
    do {
        n = mImpl->recv(mInBuffer.data() + mInBufferLen, 1024);
    } while (n < 0 && errno == EINTR);

    if (n == 0) {
        (void)handleResponse(true /* isEOS */);
        return;
    } else if (n < 0) {
        LOG(ERROR) << "recv returned error '" << strerror(errno) << "'.";
        return;
    }

    mInBufferLen += static_cast<size_t>(n);

    if (!handleResponse(false /* isEOS */)) {
        mImpl->postRecv(
                makeSafeCallback(this, &HTTPClientConnection::receiveResponse));
    }
}

bool HTTPClientConnection::handleResponse(bool isEOS) {
    if (mWebSocketMode) {
        ssize_t n = mWebSocketHandler->handleRequest(
                mInBuffer.data(), mInBufferLen, isEOS);

        if (n > 0) {
            mInBuffer.erase(mInBuffer.begin(), mInBuffer.begin() + n);
            mInBufferLen -= n;
        }

        return n <= 0;
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

    HTTPResponse response;
    if (response.setTo(mInBuffer.data(), len) < 0) {
        LOG(ERROR) << "failed to get valid server response.";

        mInBuffer.clear();
        mInBufferLen = 0;

        return true;
    } else {
        LOG(INFO)
            << "got response: "
            << response.getVersion()
            << ", "
            << response.getStatusCode()
            << ", "
            << response.getStatusMessage();

        LOG(INFO) << hexdump(mInBuffer.data(), len);

        mInBuffer.erase(mInBuffer.begin(), mInBuffer.begin() + len);
        mInBufferLen -= len;

        size_t contentLength = response.getContentLength();
        LOG(VERBOSE) << "contentLength = " << contentLength;
        assert(mInBufferLen >= contentLength);

        LOG(INFO) << hexdump(mInBuffer.data(), contentLength);
        mInBuffer.clear();

        if (response.getStatusCode() == 101) {
            mWebSocketMode = true;

            mWebSocketHandler->setOutputCallback(
                    mRemoteAddr,
                    [this](const uint8_t *data, size_t size) {
                        queueOutputData(data, size);
                    });

            const std::string msg = "\"message\":\"Hellow, world!\"";
            mWebSocketHandler->sendMessage(msg.c_str(), msg.size());

            return false;
        }
    }

    return true;
}

void HTTPClientConnection::queueOutputData(const uint8_t *data, size_t size) {
    CHECK(mRunLoop->isCurrentThread());
    std::copy(data, &data[size], std::back_inserter(mOutBuffer));

    if (!mSendPending) {
        mSendPending = true;
        mImpl->postSend(
                makeSafeCallback(this, &HTTPClientConnection::sendOutputData));
    }
}

void HTTPClientConnection::sendOutputData() {
    mSendPending = false;

    const size_t size = mOutBuffer.size();
    size_t offset = 0;

    while (offset < size) {
        ssize_t n = mImpl->send(mOutBuffer.data() + offset, size - offset);

        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }

            if (errno == EAGAIN) {
                break;
            }

            // The remote is gone (due to error), clear the output buffer and disconnect.
            offset = size;
            break;
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
        mImpl->postSend(
                makeSafeCallback(this, &HTTPClientConnection::sendOutputData));

        return;
    }
}

