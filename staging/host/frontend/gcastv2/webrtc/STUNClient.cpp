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

#include "Utils.h"

#include <webrtc/STUNClient.h>
#include <webrtc/STUNMessage.h>

#include <https/SafeCallbackable.h>
#include <https/Support.h>

#include <android-base/logging.h>

STUNClient::STUNClient(
        std::shared_ptr<RunLoop> runLoop,
        const sockaddr_in &addr,
        Callback cb)
    : mRunLoop(runLoop),
      mRemoteAddr(addr),
      mCallback(cb),
      mTimeoutToken(0),
      mNumRetriesLeft(kMaxNumRetries) {

    int sock = socket(PF_INET, SOCK_DGRAM, 0);
    makeFdNonblocking(sock);

    sockaddr_in addrV4;
    memset(addrV4.sin_zero, 0, sizeof(addrV4.sin_zero));
    addrV4.sin_family = AF_INET;
    addrV4.sin_port = 0;
    addrV4.sin_addr.s_addr = INADDR_ANY;

    int res = bind(
            sock,
            reinterpret_cast<const sockaddr *>(&addrV4),
            sizeof(addrV4));

    CHECK(!res);

    sockaddr_in tmp;
    socklen_t tmpLen = sizeof(tmp);

    res = getsockname(sock, reinterpret_cast<sockaddr *>(&tmp), &tmpLen);
    CHECK(!res);

    LOG(VERBOSE) << "local port: " << ntohs(tmp.sin_port);

    mSocket = std::make_shared<PlainSocket>(mRunLoop, sock);
}

void STUNClient::run() {
    LOG(VERBOSE) << "STUNClient::run()";

    scheduleRequest();
}

void STUNClient::onSendRequest() {
    LOG(VERBOSE) << "STUNClient::onSendRequest";

    std::vector<uint8_t> transactionID { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12 };

    STUNMessage msg(0x0001 /* Binding Request */, transactionID.data());
    msg.addFingerprint();

    ssize_t n;

    do {
        n = sendto(
            mSocket->fd(),
            msg.data(),
            msg.size(),
            0 /* flags */,
            reinterpret_cast<const sockaddr *>(&mRemoteAddr),
            sizeof(mRemoteAddr));

    } while (n < 0 && errno == EINTR);

    CHECK_GT(n, 0);

    LOG(VERBOSE) << "Sent BIND request, awaiting response";

    mSocket->postRecv(
            makeSafeCallback(this, &STUNClient::onReceiveResponse));
}

void STUNClient::onReceiveResponse() {
    LOG(VERBOSE) << "Received STUN response";

    std::vector<uint8_t> buffer(kMaxUDPPayloadSize);

    uint8_t *data = buffer.data();

    sockaddr_storage addr;
    socklen_t addrLen = sizeof(addr);

    auto n = mSocket->recvfrom(
            data, buffer.size(), reinterpret_cast<sockaddr *>(&addr), &addrLen);

    CHECK_GT(n, 0);

    STUNMessage msg(data, n);
    CHECK(msg.isValid());

    // msg.dump();

    if (msg.type() == 0x0101 /* Binding Response */) {
        const uint8_t *data;
        size_t size;
        if (msg.findAttribute(
                    0x0020 /* XOR-MAPPED-ADDRESS */,
                    reinterpret_cast<const void **>(&data),
                    &size)) {

            CHECK_EQ(size, 8u);
            CHECK_EQ(data[1], 0x01u);  // We only deal with IPv4 for now.

            static constexpr uint32_t kMagicCookie = 0x2112a442;

            uint16_t port = U16_AT(&data[2]) ^ (kMagicCookie >> 16);
            uint32_t ip = U32_AT(&data[4]) ^ kMagicCookie;

            LOG(VERBOSE) << "translated port: " << port;

            mCallback(
                    0 /* result */,
                    StringPrintf(
                        "%u.%u.%u.%u",
                        ip >> 24,
                        (ip >> 16) & 0xff,
                        (ip >> 8) & 0xff,
                        ip & 0xff));

            mRunLoop->cancelToken(mTimeoutToken);
            mTimeoutToken = 0;
        }
    }
}

void STUNClient::scheduleRequest() {
    CHECK_EQ(mTimeoutToken, 0);

    mSocket->postSend(
            makeSafeCallback(this, &STUNClient::onSendRequest));

    mTimeoutToken = mRunLoop->postWithDelay(
            kTimeoutDelay,
            makeSafeCallback(this, &STUNClient::onTimeout));

}

void STUNClient::onTimeout() {
    mTimeoutToken = 0;

    if (mNumRetriesLeft == 0) {
        mCallback(-ETIMEDOUT, "");
        return;
    }

    --mNumRetriesLeft;
    scheduleRequest();
}

