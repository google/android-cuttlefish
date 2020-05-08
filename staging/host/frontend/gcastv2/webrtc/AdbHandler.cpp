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

#include <webrtc/AdbHandler.h>

#include "Utils.h"

#include <https/BaseConnection.h>
#include <https/Support.h>

#include <android-base/logging.h>

#include <unistd.h>

using namespace android;

struct AdbHandler::AdbConnection : public BaseConnection {
    explicit AdbConnection(
            AdbHandler *parent,
            std::shared_ptr<RunLoop> runLoop,
            int sock);

    void send(const void *_data, size_t size);

protected:
    long processClientRequest(const void *data, size_t size) override;
    void onDisconnect(int err) override;

private:
    AdbHandler *mParent;
};

////////////////////////////////////////////////////////////////////////////////

AdbHandler::AdbConnection::AdbConnection(
        AdbHandler *parent,
        std::shared_ptr<RunLoop> runLoop,
        int sock)
    : BaseConnection(runLoop, sock),
      mParent(parent) {
}

// Thanks for calling it a crc32, adb documentation!
static uint32_t computeNotACrc32(const void *_data, size_t size) {
    auto data = static_cast<const uint8_t *>(_data);
    uint32_t sum = 0;
    for (size_t i = 0; i < size; ++i) {
        sum += data[i];
    }

    return sum;
}

static int verifyAdbHeader(
        const void *_data, size_t size, size_t *_payloadLength) {
    auto data = static_cast<const uint8_t *>(_data);

    *_payloadLength = 0;

    if (size < 24) {
        return -EAGAIN;
    }

    uint32_t command = U32LE_AT(data);
    uint32_t magic = U32LE_AT(data + 20);

    if (command != (magic ^ 0xffffffff)) {
        return -EINVAL;
    }

    uint32_t payloadLength = U32LE_AT(data + 12);

    if (size < 24 + payloadLength) {
        return -EAGAIN;
    }

    auto payloadCrc = U32LE_AT(data + 16);
    auto crc32 = computeNotACrc32(data + 24, payloadLength);

    if (payloadCrc != crc32) {
        return -EINVAL;
    }

    *_payloadLength = payloadLength;

    return 0;
}

long AdbHandler::AdbConnection::processClientRequest(
        const void *_data, size_t size) {
    auto data = static_cast<const uint8_t *>(_data);

    LOG(VERBOSE)
        << "AdbConnection::processClientRequest (size = " << size << ")";

    LOG(VERBOSE) << hexdump(data, size);

    size_t payloadLength;
    int err = verifyAdbHeader(data, size, &payloadLength);

    if (err) {
        return err;
    }

    mParent->send_to_client_(data, payloadLength + 24);
    return payloadLength + 24;
}

void AdbHandler::AdbConnection::onDisconnect(int err) {
    LOG(INFO) << "AdbConnection::onDisconnect(err=" << err << ")";

    mParent->send_to_client_(nullptr /* data */, 0 /* size */);
}

void AdbHandler::AdbConnection::send(const void *_data, size_t size) {
    BaseConnection::send(_data, size);
}

////////////////////////////////////////////////////////////////////////////////

AdbHandler::AdbHandler(
        std::shared_ptr<RunLoop> runLoop,
        const std::string &adb_host_and_port,
        std::function<void(const uint8_t*, size_t)>  send_to_client)
    : mRunLoop(runLoop),
      mSocket(-1),
      send_to_client_(send_to_client) {
    LOG(INFO) << "Connecting to " << adb_host_and_port;

    auto err = setupSocket(adb_host_and_port);
    CHECK(!err);

    mAdbConnection = std::make_shared<AdbConnection>(this, mRunLoop, mSocket);
}

AdbHandler::~AdbHandler() {
    if (mSocket >= 0) {
        close(mSocket);
        mSocket = -1;
    }
}

void AdbHandler::run() {
    mAdbConnection->run();
}

int AdbHandler::setupSocket(const std::string &adb_host_and_port) {
    auto colonPos = adb_host_and_port.find(':');
    if (colonPos == std::string::npos) {
        return -EINVAL;
    }

    auto host = adb_host_and_port.substr(0, colonPos);

    const char *portString = adb_host_and_port.c_str() + colonPos + 1;
    char *end;
    unsigned long port = strtoul(portString, &end, 10);

    if (end == portString || *end != '\0' || port > 65535) {
        return -EINVAL;
    }

    int err;

    int sock = socket(PF_INET, SOCK_STREAM, 0);

    if (sock < 0) {
        err = -errno;
        goto bail;
    }

    makeFdNonblocking(sock);

    sockaddr_in addr;
    memset(addr.sin_zero, 0, sizeof(addr.sin_zero));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = inet_addr(host.c_str());
    addr.sin_port = htons(port);

    if (connect(sock,
                reinterpret_cast<const sockaddr *>(&addr),
                sizeof(addr)) < 0
            && errno != EINPROGRESS) {
        err = -errno;
        goto bail2;
    }

    mSocket = sock;

    return 0;

bail2:
    close(sock);
    sock = -1;

bail:
    return err;
}

void AdbHandler::handleMessage(const uint8_t *msg, size_t len) {
    LOG(VERBOSE) << hexdump(msg, len);

    size_t payloadLength;
    int err = verifyAdbHeader(msg, len, &payloadLength);

    if (err || len != 24 + payloadLength) {
        LOG(ERROR) << "Not a valid adb message.";
        return;
    }

    mAdbConnection->send(msg, len);
}

