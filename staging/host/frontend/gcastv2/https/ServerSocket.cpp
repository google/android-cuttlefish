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

#include <https/ServerSocket.h>

#include <https/ClientSocket.h>
#include <https/RunLoop.h>
#include <https/SafeCallbackable.h>
#include <https/Support.h>

#include <android-base/logging.h>

#include <arpa/inet.h>
#include <fcntl.h>
#include <iostream>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

ServerSocket::ServerSocket(
        HTTPServer *server,
        TransportType transportType,
        const char *iface,
        uint16_t port,
        const std::optional<std::string> &certificate_pem_path,
        const std::optional<std::string> &private_key_pem_path)
    : mInitCheck(-ENODEV),
      mServer(server),
      mCertificatePath(certificate_pem_path),
      mPrivateKeyPath(private_key_pem_path),
      mSocket(-1),
      mTransportType(transportType) {
    if (mTransportType == TransportType::TLS) {
        CHECK(mCertificatePath.has_value());
        CHECK(mPrivateKeyPath.has_value());
    }

    sockaddr_in addr;
    int res;

    mSocket = socket(PF_INET, SOCK_STREAM, 0);
    if (mSocket < 0) {
        mInitCheck = -errno;
        goto bail;
    }

    makeFdNonblocking(mSocket);

    static constexpr int yes = 1;
    res = setsockopt(mSocket, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
    if (res < 0) {
        mInitCheck = -errno;
        goto bail2;
    }

    memset(addr.sin_zero, 0, sizeof(addr.sin_zero));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = inet_addr(iface);

    res = bind(mSocket, reinterpret_cast<sockaddr *>(&addr), sizeof(addr));
    if (res < 0) {
        mInitCheck = -errno;
        goto bail2;
    }

    res = listen(mSocket, 4);
    if (res < 0) {
        mInitCheck = -errno;
        goto bail2;
    }

    mInitCheck = 0;
    return;

bail2:
    close(mSocket);
    mSocket = -1;

bail:
    ;
}

ServerSocket::~ServerSocket() {
    if (mSocket >= 0) {
        close(mSocket);
        mSocket = -1;
    }
}

int ServerSocket::initCheck() const {
    return mInitCheck;
}

ServerSocket::TransportType ServerSocket::transportType() const {
    return mTransportType;
}

int ServerSocket::run(std::shared_ptr<RunLoop> rl) {
    if (mInitCheck < 0) {
        return mInitCheck;
    }

    if (mRunLoop) {
        return -EBUSY;
    }

    mRunLoop = rl;
    mRunLoop->postSocketRecv(
            mSocket,
            makeSafeCallback(this, &ServerSocket::acceptIncomingConnection));

    return 0;
}

void ServerSocket::acceptIncomingConnection() {
    sockaddr_in addr;
    socklen_t addrLen = sizeof(addr);

    int s = accept(mSocket, reinterpret_cast<sockaddr *>(&addr), &addrLen);

    if (s >= 0) {
        uint32_t ip = ntohl(addr.sin_addr.s_addr);

        LOG(VERBOSE)
            << "Accepted incoming connection from "
            << (ip >> 24)
            << "."
            << ((ip >> 16) & 0xff)
            << "."
            << ((ip >> 8) & 0xff)
            << "."
            << (ip & 0xff)
            << ":"
            << ntohs(addr.sin_port);

        makeFdNonblocking(s);

        auto clientSocket =
            std::make_shared<ClientSocket>(mRunLoop, mServer, this, addr, s);

        clientSocket->run();

        mClientSockets.push_back(clientSocket);
    }

    mRunLoop->postSocketRecv(
            mSocket,
            makeSafeCallback(this, &ServerSocket::acceptIncomingConnection));
}

void ServerSocket::onClientSocketClosed(int sock) {
    for (size_t i = mClientSockets.size(); i--;) {
        if (mClientSockets[i]->fd() == sock) {
            LOG(VERBOSE) << "Closing client connection.";
            mClientSockets.erase(mClientSockets.begin() + i);
            break;
        }
    }
}

std::optional<std::string> ServerSocket::certificate_pem_path() const {
    return mCertificatePath;
}

std::optional<std::string> ServerSocket::private_key_pem_path() const {
    return mPrivateKeyPath;
}

